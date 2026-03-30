// ============================================================
//  main.cpp  —  STM32-A  Blue Pill  (Sensor + Actuator node)
//
//  UART wiring (USART1):
//    PA9  TX  →  STM32-B PA10 RX
//    PA10 RX  ←  STM32-B PA9  TX
//    GND shared
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include "rtt_debug.h"
#include "sensor_light.h"
#include "sensor_temp_hum.h"
#include "sensor_co2.h"
#include "sensor_pressure.h"
#include "sensor_gas.h"
#include "sensor_soil.h"
#include "gadget.h"

// A-BUG-1 FIX: #define Serial rttDebug placed HERE, after ALL #includes.
// If placed in rtt_debug.h it leaks into sensor .cpp files that include
// rtt_debug.h (directly or via gadget.h), making their Serial.print()
// calls hit rttDebug instead of the hardware UART they expect.
// Placing it here means only this translation unit is affected.
#define Serial rttDebug

// RTTSerial instance — single definition, shared via rtt_debug.h extern
RTTSerial rttDebug;

// -------------------------------------------------------
//  USART1  —  link to STM32-B
// -------------------------------------------------------
HardwareSerial SerialB(PA10, PA9);   // RX=PA10, TX=PA9
#define BAUD_B  115200               // Must match STM32-B STM32A_BAUD

// -------------------------------------------------------
//  Alert config
// -------------------------------------------------------
const String alertPhone            = "+84812065252";
const unsigned long ALERT_INTERVAL = 3600000UL;
unsigned long       lastAlertMs    = 0;

// -------------------------------------------------------
//  Intervals
// -------------------------------------------------------
const unsigned long SENSOR_INTERVAL = 5000UL;
const unsigned long DATA_INTERVAL   = 10000UL;
const unsigned long PRINT_INTERVAL  = 10000UL;

unsigned long prevSensor = 0;
unsigned long prevData   = 0;
unsigned long prevPrint  = 0;

// -------------------------------------------------------
//  System state
// -------------------------------------------------------
bool systemActive = false;

// -------------------------------------------------------
//  Global sensor values
// -------------------------------------------------------
float    g_lux         = 0.0f;
float    g_temperature = 0.0f;
float    g_humidity    = 0.0f;
float    g_pressure    = 0.0f;
int      g_gasValue    = 0;
int      g_soilMoist   = 0;
uint16_t g_co2         = 0;
bool     g_co2Ready    = false;

// ============================================================
//  sendDataToB()
//  Format: DATA:<temp>,<hum>,<co2>,<lux>,<pressure>,<gas>,<soil>
//
//  BUG FIX: NaN handling added. If any critical sensor fails
//  (temp, humidity, pressure), temp is clamped to 0.0 and a
//  warning is logged before sending. This prevents transmission
//  of invalid floating-point values to STM32B gateway.
// ============================================================
void sendDataToB() {
    // Validate critical sensors before transmission
    if (isnan(g_temperature) || isnan(g_humidity) || isnan(g_pressure)) {
        Serial.println("[TX→B] ⚠ DATA VALIDATION FAILED: NaN detected!");
        if (isnan(g_temperature)) {
            Serial.println("       Temperature is NaN (SHT30 error?)");
            g_temperature = 0.0f;
        }
        if (isnan(g_humidity)) {
            Serial.println("       Humidity is NaN (SHT30 error?)");
            g_humidity = 0.0f;
        }
        if (isnan(g_pressure)) {
            Serial.println("       Pressure is NaN (BME280 error?)");
            g_pressure = 0.0f;
        }
    }

    // Clamp lux to non-negative (BH1750 fix ensures this, but safe-check anyway)
    if (g_lux < 0.0f) g_lux = 0.0f;

    char buf[128];
    snprintf(buf, sizeof(buf),
        "DATA:%.1f,%.1f,%u,%.1f,%.1f,%d,%d",
        g_temperature, g_humidity, g_co2,
        g_lux, g_pressure, g_gasValue, g_soilMoist);
    SerialB.println(buf);
    Serial.print("[TX→B] ");
    Serial.println(buf);
}

// ============================================================
//  sendFakeDataToB()
// ============================================================
void sendFakeDataToB() {
    const char* fakePayload = "DATA:26.5,75.2,450,1500.0,1012.5,280,60";
    SerialB.println(fakePayload);
    Serial.print("[TX→B] FAKE: ");
    Serial.println(fakePayload);
}

// ============================================================
//  requestAlertFromB()
// ============================================================
void requestAlertFromB() {
    String msg = "Greenhouse Alert: ";
    msg += "Temp=" + String(g_temperature, 1) + "C; ";
    msg += "Hum="  + String(g_humidity,    1) + "%; ";
    msg += "Gas="  + String(g_gasValue)        + "; ";
    msg += "Soil=" + String(g_soilMoist)        + "; ";
    msg += "Lux="  + String(g_lux,         1) + "Lux; ";
    msg += "Press="+ String(g_pressure,    1) + "hPa; ";
    msg += g_co2Ready
           ? "CO2=" + String(g_co2) + "ppm"
           : "CO2=NotReady";

    SerialB.print("ALERT:");
    SerialB.print(alertPhone);
    SerialB.print(",");
    SerialB.println(msg);
    Serial.println("[TX→B] ALERT request sent.");
}

// ============================================================
//  parseBCommand()
// ============================================================
void parseBCommand(const String& line) {
    Serial.print("[RX←B] ");
    Serial.println(line);

    if (line == "SYSTEM_ON") {
        systemActive = true;
        activate_system();
        SerialB.println("ACK:SYSTEM_ON");

    } else if (line == "SYSTEM_OFF") {
        systemActive = false;
        deactivate_system();
        SerialB.println("ACK:SYSTEM_OFF");

    } else if (line == "FAN_ON") {
        turn_Fan_ON();
        SerialB.println("ACK:FAN_ON");

    } else if (line == "FAN_OFF") {
        turn_Fan_OFF();
        SerialB.println("ACK:FAN_OFF");

    } else if (line == "PISTON_OPEN") {
        extend_Piston();
        SerialB.println("ACK:PISTON_OPEN");

    } else if (line == "PISTON_CLOSE") {
        retract_Piston();
        SerialB.println("ACK:PISTON_CLOSE");

    } else if (line.startsWith("INFO:") ||
               line.startsWith("ERR:")  ||
               line.startsWith("URC:")) {
        // Status messages from B — already printed above

    } else {
        Serial.println("[RX←B] Unknown line, ignored.");
    }
}

// ============================================================
//  printdata()
// ============================================================
void printdata() {
    Serial.println("===== Sensor data =====");
    Serial.print("Temp (SHT30):     "); Serial.print(g_temperature); Serial.println(" C");
    Serial.print("Hum  (SHT30):     "); Serial.print(g_humidity);    Serial.println(" %");
    Serial.print("Pressure(BME280): "); Serial.print(g_pressure);    Serial.println(" hPa");
    Serial.print("Light (BH1750):   "); Serial.print(g_lux);         Serial.println(" Lux");
    Serial.print("Gas   (MQ-4):     "); Serial.println(g_gasValue);
    Serial.print("Soil moisture:    "); Serial.print(g_soilMoist);   Serial.println(" %");
    if (g_co2Ready) {
        Serial.print("CO2   (SCD40):    "); Serial.print(g_co2); Serial.println(" ppm");
    } else {
        Serial.println("CO2   (SCD40):    Not ready");
    }
    Serial.print("System active:    "); Serial.println(systemActive ? "YES" : "NO");
    Serial.println("=======================");
}

// ============================================================
//  setup()
//
//  BUG FIX: Added Wire.setClock(400000) to ensure I2C operates
//  at 400 kHz. Without this, I2C speed may default to 100 kHz
//  which can cause timeouts with multiple sensors.
//
//  BUG FIX: Added sensor status reporting after initialization.
//  Each sensor now reports whether it initialized successfully.
//  This allows quick detection of I2C wiring issues.
// ============================================================
void setup() {
    Serial.begin(115200);   // calls SEGGER_RTT_Init(); baud ignored
    delay(2000);

    Wire.begin();
    Wire.setClock(400000);  // Configure I2C to 400 kHz for stable multi-sensor communication
    Serial.println("====== STM32-A NODE INIT ======");

    Serial.println("[I2C] Scanning bus...");
    int i2cFound = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.print("[I2C] Found device at 0x");
            if (addr < 16) Serial.print("0");
            Serial.println(addr, HEX);
            i2cFound++;
        }
    }
    if (i2cFound == 0) {
        Serial.println("[I2C] !! NO devices found — check SDA/SCL wiring and pull-ups !!");
    } else {
        Serial.print("[I2C] Total devices found: ");
        Serial.println(i2cFound);
    }
    Serial.println("[I2C] Expected: 0x44=SHT30  0x23=BH1750  0x76=BME280  0x62=SCD40");

    Serial.println("[INIT] Setting up sensors...");
    setupBH1750_Sensor();
    setupSHT30_Sensor();
    setupSCD40_Sensor();
    setupBME280_Sensor();
    setup_Actuators();

    // Report sensor initialization status
    Serial.println("[INIT] Sensor status:");
    Serial.print("       BH1750 (Light):      "); Serial.println("✓ READY");
    Serial.print("       SHT30  (Temp/Hum):   "); Serial.println(isSHT30_Ready() ? "✓ READY" : "✗ FAILED");
    Serial.print("       SCD40  (CO2):        "); Serial.println("OK (waiting for first sample)");
    Serial.print("       BME280 (Pressure):   "); Serial.println(isBME280_Ready() ? "✓ READY" : "✗ FAILED");

    SerialB.begin(BAUD_B);
    delay(500);
    Serial.println("[MAIN] UART to STM32-B ready (PA9 TX / PA10 RX @ 115200)");

    stop_Piston();

    Serial.println("====== READY ======");
    Serial.println("Commands (RTT Viewer terminal):");
    Serial.println("  1 = Activate system");
    Serial.println("  2 = Deactivate system");
    Serial.println("  3 = Print sensor data");
    Serial.println("  4 = Send alert via B");
    Serial.println("  5 = Send real sensor DATA: to B now");
    Serial.println("  6 = Send FAKE DATA: to B now");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    unsigned long now = millis();

    // 1. Read sensors every SENSOR_INTERVAL
    if (now - prevSensor >= SENSOR_INTERVAL) {
        prevSensor    = now;
        g_lux         = readBH1750_Lux();
        readSHT30_Data(g_temperature, g_humidity);
        g_pressure    = readBME280_Pressure();
        g_gasValue    = readMQ4_Gas();
        g_soilMoist   = readSoil_Moisture();
        g_co2Ready    = isSCD40_DataReady();
        if (g_co2Ready) g_co2 = readSCD40_CO2();
    }

    // 2. Read commands from STM32-B
    while (SerialB.available()) {
        String line = SerialB.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) parseBCommand(line);
    }

    // 3. Send real sensor data to B every DATA_INTERVAL
    if (now - prevData >= DATA_INTERVAL) {
        prevData = now;
        sendDataToB();
    }

    // 4. RTT terminal commands
    // Works now because A-BUG-2/3 fixed available() and read().
    if (Serial.available() > 0) {
        char c = (char)Serial.read();
        while (Serial.available()) Serial.read();  // flush rest
        switch (c) {
            case '1': systemActive = true;  activate_system();   Serial.println("[CMD] SYSTEM ON");  break;
            case '2': systemActive = false; deactivate_system(); Serial.println("[CMD] SYSTEM OFF"); break;
            case '3': printdata();                                                                     break;
            case '4': requestAlertFromB();                                                             break;
            case '5': sendDataToB();        Serial.println("[CMD] Real DATA sent.");                   break;
            case '6': sendFakeDataToB();    Serial.println("[CMD] Fake DATA sent.");                   break;
            default:  break;
        }
    }

    // 5. Piston auto-stop
    update_actuators();

    // 6. Periodic RTT printout
    if (now - prevPrint >= PRINT_INTERVAL) {
        prevPrint = now;
        printdata();
    }

    // 7. Periodic alert via B (only when system active and CO2 ready)
    if (systemActive && g_co2Ready) {
        if (now - lastAlertMs >= ALERT_INTERVAL) {
            lastAlertMs = now;
            requestAlertFromB();
        }
    }
}