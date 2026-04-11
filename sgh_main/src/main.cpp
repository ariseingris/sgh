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
#include "i2c_recover.h"
#include "sensor_light.h"
#include "sensor_temp_hum.h"
#include "sensor_co2.h"
#include "sensor_pressure.h"
#include "sensor_gas.h"
#include "sensor_soil.h"
#include "gadget.h"

// RTTSerial instance — single definition, shared via rtt_debug.h extern
RTTSerial rttDebug;

// Redirect Serial → rttDebug so all Serial.xxx calls go to SEGGER RTT.
// Per rtt_debug.h rules: this #define MUST appear here (in main.cpp),
// AFTER all #includes. Placing it in the header would corrupt library code.
#define Serial rttDebug

// -------------------------------------------------------
//  USART1  —  link to STM32-B
//  FIX: baud was 57600 on A but 115200 on B → mismatch.
//  Both sides must match. Using 115200.
// -------------------------------------------------------
HardwareSerial SerialB(PA10, PA9);              // RX=PA10, TX=PA9  (USART1 to STM32-B)
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

// -------------------------------------------------------
//  I2C watchdog
//  Tracks consecutive zero-readings. If all I2C sensors
//  return 0 for I2C_FAIL_THRESHOLD cycles, the bus is
//  likely locked up → trigger recovery + re-init.
// -------------------------------------------------------
static int  i2cFailCount              = 0;
static const int I2C_FAIL_THRESHOLD   = 3;    // failures before recovery
static unsigned long lastI2CRecoverMs = 0;
static const unsigned long I2C_RECOVER_COOLDOWN = 30000UL; // max once per 30s

// Called after each sensor read cycle to check I2C health.
// FIX 3: Replaced allZero check (false-positives at night when lux=0,
// or at 0°C) with a pressure-anchored check. Atmospheric pressure is
// never 0 hPa — if the BME280 returns 0 it has locked up. We require
// BOTH the pressure and the temp/humidity pair to fail simultaneously
// before declaring a bus lockup, which eliminates single-sensor glitches.
static void checkI2CHealth() {
    // Pressure is the most reliable lockup indicator: never 0 in atmosphere.
    // Require pressure AND (temp+hum) both zero to avoid false positives from
    // a single momentary read error on one sensor.
    bool pressureFailed = (g_pressure == 0.0f);
    bool tempHumFailed  = (g_temperature == 0.0f && g_humidity == 0.0f);

    if (pressureFailed && tempHumFailed) {
        i2cFailCount++;
        rttDebug.print("[I2C] Watchdog: multi-sensor zero count=");
        rttDebug.println(i2cFailCount);
    } else {
        i2cFailCount = 0;  // reset on any good reading
    }

    unsigned long now = millis();
    if (i2cFailCount >= I2C_FAIL_THRESHOLD &&
        (now - lastI2CRecoverMs) >= I2C_RECOVER_COOLDOWN) {

        lastI2CRecoverMs = now;
        i2cFailCount     = 0;
        rttDebug.println("[I2C] BUS LOCKUP DETECTED — attempting recovery...");

        bool ok = recoverI2C();
        rttDebug.print("[I2C] Bus recovery: ");
        rttDebug.println(ok ? "SDA released" : "SDA still stuck!");

        int found = scanI2C();
        rttDebug.print("[I2C] Devices after recovery: ");
        rttDebug.println(found);

        if (found > 0) {
            // Re-init all I2C sensors
            rttDebug.println("[I2C] Re-initialising sensors...");
            setupBH1750_Sensor();
            setupSHT30_Sensor();
            setupSCD40_Sensor();
            setupBME280_Sensor();
            rttDebug.println("[I2C] Sensors re-initialised.");
        } else {
            rttDebug.println("[I2C] !! No devices found after recovery. Check wiring !!");
        }
    }
}

// ============================================================
//  sendDataToB()
//  Format: DATA:<temp>,<hum>,<co2>,<lux>,<pressure>,<gas>,<soil>
//  This is the exact format STM32-B's onDataFromA() expects.
// ============================================================
void sendDataToB() {
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
//  Sends hardcoded test values so you can verify the full
//  pipeline (A→B UART → B parses → B publishes to HiveMQ)
//  without needing real sensors connected.
//  Format is identical to sendDataToB().
// ============================================================
void sendFakeDataToB() {
    // Realistic greenhouse values for testing
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
        // FIX: removed `systemActive = true` — activate_system() now sets it
        activate_system();
        SerialB.println("ACK:SYSTEM_ON");

    } else if (line == "SYSTEM_OFF") {
        // FIX: removed `systemActive = false` — deactivate_system() now sets it
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
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(2000);

    Wire.begin();
    Serial.println("====== STM32-A NODE INIT ======");

    // -------------------------------------------------------
    //  I2C Scanner — runs once at boot to confirm wiring.
    //  Expected: 0x44 (SHT30), 0x23 (BH1750), 0x76 (BME280),
    //            0x62 (SCD40)
    //  If a sensor is missing here, its values will be 0.
    // -------------------------------------------------------
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
        rttDebug.println("[I2C] !! NO devices found — check SDA/SCL wiring and pull-ups !!");
    } else {
        rttDebug.print("[I2C] Total devices found: ");
        rttDebug.println(i2cFound);
    }
    Serial.println("[I2C] Expected: 0x44=SHT30  0x23=BH1750  0x76=BME280  0x62=SCD40");

    setupBH1750_Sensor();
    setupSHT30_Sensor();
    setupSCD40_Sensor();
    setupBME280_Sensor();
    setup_Actuators();

    SerialB.begin(BAUD_B);
    delay(500);
    Serial.println("[MAIN] UART to STM32-B ready (PA9 TX / PA10 RX @ 115200)");

    stop_Piston();

    rttDebug.println("====== READY ======");
    rttDebug.println("Commands:");
    rttDebug.println("  1 = Activate system");
    rttDebug.println("  2 = Deactivate system");
    rttDebug.println("  3 = Print sensor data");
    rttDebug.println("  4 = Send alert via B");
    rttDebug.println("  5 = Send real sensor DATA: to B now");
    rttDebug.println("  6 = Send FAKE DATA: to B now");  // <-- new
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

        // I2C watchdog — auto-recovers bus if sensors lock up
        checkI2CHealth();
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
 
    // 4. Serial (→ rttDebug via macro) debug commands — RTT terminal input.
    if (Serial.available() > 0) {
        char c = Serial.read();
        while (Serial.available()) Serial.read();   // flush rest
        switch (c) {
            case '1': activate_system();   Serial.println("[CMD] SYSTEM ON");       break;
            case '2': deactivate_system(); Serial.println("[CMD] SYSTEM OFF");      break;
            case '3': printdata();                                                                          break;
            case '4': requestAlertFromB();                                                                  break;
            case '5': sendDataToB();        Serial.println("[CMD] Real DATA sent.");                        break;
            case '6': sendFakeDataToB();    Serial.println("[CMD] Fake DATA sent.");                        break;
            default:  break;
        }
    }

    // 5. Piston auto-stop
    update_actuators();

    // 6. Periodic USB printout
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