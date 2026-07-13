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
#include <IWatchdog.h>
#include "rtt_debug.h"
// config.h removed — all settings inlined below
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
//  FLAW-2 FIX: phone number moved to STM32-B config.h.
//  STM32-A only sends the message body.
// -------------------------------------------------------
const unsigned long ALERT_INTERVAL = 3600000UL;
unsigned long       lastAlertMs    = 0;

// -------------------------------------------------------
//  Intervals
// -------------------------------------------------------
const unsigned long SENSOR_INTERVAL = 6000UL;
const unsigned long DATA_INTERVAL   = 10000UL;
const unsigned long PRINT_INTERVAL  = 10000UL;

unsigned long prevSensor = 0;
unsigned long prevData   = 0;
unsigned long prevPrint  = 0;

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
// FIX 3: Pressure-anchored dual-sensor check. Requires BOTH pressure
// AND temp/humidity to return the -999.0f fault sentinel before declaring
// a bus lockup — eliminates single-sensor glitches.
// PHASE 2 FIX: Checks < 0 / < -100 instead of == 0 so that:
//   - pressure: -999.0 < 0 ✓ (real pressure is always ≥ 100 hPa)
//   - temperature: -999.0 < -100 ✓ (real temp ≥ -40°C > -100°C)
//   - humidity: -999.0 < 0 ✓ (real humidity ≥ 0%)
static void checkI2CHealth() {
    bool pressureFailed = (g_pressure < 0.0f);
    bool tempHumFailed  = (g_temperature < -100.0f && g_humidity < 0.0f);

    if (pressureFailed && tempHumFailed) {
        i2cFailCount++;
        rttDebug.print("[I2C] Watchdog: multi-sensor zero count=");
        rttDebug.println(i2cFailCount);
    } else {
        i2cFailCount = 0;
    }

    unsigned long now = millis();
    if (i2cFailCount >= I2C_FAIL_THRESHOLD &&
        (now - lastI2CRecoverMs) >= I2C_RECOVER_COOLDOWN) {

        lastI2CRecoverMs = now;
        i2cFailCount     = 0;
        rttDebug.println("[I2C] BUS LOCKUP DETECTED — attempting recovery...");

        // FW-4 FIX: Pet watchdog before potentially long recovery+scan
        // (recovery + scanI2C + re-init can take 2–3s, watchdog is 4s)
        IWatchdog.reload();

        bool ok = recoverI2C();
        rttDebug.print("[I2C] Bus recovery: ");
        rttDebug.println(ok ? "SDA released" : "SDA still stuck!");

        IWatchdog.reload();  // pet again before scan
        int found = scanI2C();
        rttDebug.print("[I2C] Devices after recovery: ");
        rttDebug.println(found);

        if (found > 0) {
            rttDebug.println("[I2C] Re-initialising sensors...");
            setupBH1750_Sensor();
            setupSHT30_Sensor();
            setupSCD40_Sensor();
            setupBME280_Sensor();
            rttDebug.println("[I2C] Sensors re-initialised.");
        } else {
            rttDebug.println("[I2C] !! No devices found after recovery. Check wiring !!");
        }
        IWatchdog.reload();  // pet after re-init
    }
}

// ============================================================
//  sendDataToB()
//  FLAW-3 FIX: Sends JSON directly instead of CSV.
//  STM32-B publishes this payload verbatim — no parsing needed.
//  Format: DATA:{"ts":...,"temp":...,"hum":...,"co2":...,...}
// ============================================================
void sendDataToB() {
    // FW-1 FIX: Use dtostrf() on stack instead of heap-allocating String(float,1).
    // Eliminates 4 malloc/free calls per 10-second cycle — prevents heap fragmentation.
    unsigned long ts = millis() / 1000UL;
    char sTemp[8], sHum[8], sLux[10], sPress[10];
    dtostrf(g_temperature, 1, 1, sTemp);
    dtostrf(g_humidity,    1, 1, sHum);
    dtostrf(g_lux,         1, 1, sLux);
    dtostrf(g_pressure,    1, 1, sPress);

    // CO2 FIX: build co2 field separately as a string so we can send
    // "null" when not ready, instead of transmitting a stale/old value.
    // STM32-B / mqttBridge.js already treat co2:null as "no reading".
    char sCo2[8];
    if (g_co2Ready) {
        snprintf(sCo2, sizeof(sCo2), "%u", g_co2);
    } else {
        strcpy(sCo2, "null");
    }

    char buf[280];
    snprintf(buf, sizeof(buf),
        "DATA:{\"ts\":%lu,\"temp\":%s,\"hum\":%s,\"co2\":%s,"
        "\"lux\":%s,\"pressure\":%s,\"gas\":%d,\"soil\":%d,"
        "\"fan\":%d,\"piston\":%d}",
        ts, sTemp, sHum, sCo2,
        sLux, sPress, g_gasValue, g_soilMoist,
        (int)getFanState(), (int)getPistonState());

    SerialB.println(buf);
    Serial.print("[TX→B] ");
    Serial.println(buf);
}

// ============================================================
//  sendFakeDataToB()
//  FLAW-3 FIX: Sends JSON format matching sendDataToB().
// ============================================================
void sendFakeDataToB() {
    const char* fakePayload =
        "DATA:{\"ts\":0,\"temp\":26.5,\"hum\":75.2,\"co2\":450,"
        "\"lux\":1500.0,\"pressure\":1012.5,\"gas\":280,\"soil\":60,"
        "\"fan\":1,\"piston\":1}";
    SerialB.println(fakePayload);
    Serial.print("[TX→B] FAKE: ");
    Serial.println(fakePayload);
}

// ============================================================
//  requestAlertFromB()
//  FLAW-2 FIX: sends ALERT:<msg> without phone number.
//  Phone is now owned by STM32-B (config.h).
// ============================================================
void requestAlertFromB() {
    // FW-2 FIX: Use dtostrf() instead of %.1f to avoid linking _printf_float
    // (which adds ~10KB to flash on newlib-nano, or silently outputs nothing).
    char sTemp[8], sHum[8], sLux[10], sPress[10];
    dtostrf(g_temperature, 1, 1, sTemp);
    dtostrf(g_humidity,    1, 1, sHum);
    dtostrf(g_lux,         1, 1, sLux);
    dtostrf(g_pressure,    1, 1, sPress);

    char msg[200];
    int n = snprintf(msg, sizeof(msg),
        "Greenhouse Alert: Temp=%sC; Hum=%s%%; Gas=%d; Soil=%d; Lux=%sLux; Press=%shPa; ",
        sTemp, sHum, g_gasValue, g_soilMoist, sLux, sPress);
    if (g_co2Ready) {
        snprintf(msg + n, sizeof(msg) - n, "CO2=%uppm", g_co2);
    } else {
        snprintf(msg + n, sizeof(msg) - n, "CO2=NotReady");
    }

    SerialB.print("ALERT:");
    SerialB.println(msg);
    Serial.println("[TX→B] ALERT request sent.");
}

// ============================================================
//  parseBCommand()
// ============================================================
// FW-8 FIX: accepts const char* to avoid heap-allocating String copy.
void parseBCommand(const char* line) {
    Serial.print("[RX←B] ");
    Serial.println(line);

    if (strcmp(line, "SYSTEM_ON") == 0) {
        enterMeasuring();
        SerialB.println("ACK:SYSTEM_ON");

    } else if (strcmp(line, "SYSTEM_OFF") == 0) {
        enterIdle();
        SerialB.println("ACK:SYSTEM_OFF");

    } else if (strcmp(line, "FAN_ON") == 0) {
        turn_Fan_ON();
        SerialB.println("ACK:FAN_ON");

    } else if (strcmp(line, "FAN_OFF") == 0) {
        turn_Fan_OFF();
        SerialB.println("ACK:FAN_OFF");

    } else if (strcmp(line, "PISTON_OPEN") == 0) {
        extend_Piston();
        SerialB.println("ACK:PISTON_OPEN");

    } else if (strcmp(line, "PISTON_CLOSE") == 0) {
        retract_Piston();
        SerialB.println("ACK:PISTON_CLOSE");

    } else if (strncmp(line, "INFO:", 5) == 0 ||
               strncmp(line, "ERR:",  4) == 0 ||
               strncmp(line, "URC:",  4) == 0) {
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
    const char* stateNames[] = { "IDLE", "MOVING", "MEASURING" };
    Serial.print("State:            "); Serial.println(stateNames[getCurrentState()]);
    Serial.println("=======================");
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(2000);

    Wire.begin();
    Wire.setClock(100000); // 400 kHz fast mode — required for reliable multi-sensor I2C
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
    initMQ4_Warmup(); // Start MQ-4 warm-up timer

    SerialB.begin(BAUD_B);
    delay(500);
    Serial.println("[MAIN] UART to STM32-B ready (PA9 TX / PA10 RX @ 115200)");
    SerialB.println("INFO:BOOT");   // lets STM32-B detect mid-session resets

    stop_Piston();

    rttDebug.println("====== READY ======");
    rttDebug.println("Commands:");
    rttDebug.println("  1 = Force MEASURING state");
    rttDebug.println("  2 = Force IDLE state");
    rttDebug.println("  3 = Print sensor data");
    rttDebug.println("  4 = Send alert via B");
    rttDebug.println("  5 = Send real sensor DATA: to B now");
    rttDebug.println("  6 = Send FAKE DATA: to B now");

    // Watchdog: 4-second timeout. Must call IWatchdog.reload() in loop().
    IWatchdog.begin(4000000); // 4 seconds in microseconds
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    IWatchdog.reload(); // Pet the watchdog every loop iteration

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

    // 2. Read commands from STM32-B (non-blocking)
    //    FW-6 FIX: Added discard flag — on buffer overflow, skip all
    //    remaining bytes until next newline instead of parsing the tail.
    //    FW-8 FIX: Pass char* directly to parseBCommand — no String copy.
    {
        static char cmdBuf[80];
        static uint8_t cmdPos = 0;
        static bool discarding = false;
        while (SerialB.available()) {
            char c = (char)SerialB.read();
            if (c == '\n' || c == '\r') {
                if (!discarding && cmdPos > 0) {
                    cmdBuf[cmdPos] = '\0';
                    parseBCommand(cmdBuf);
                }
                cmdPos = 0;
                discarding = false;
                continue;
            }
            if (discarding) continue;
            if (cmdPos < sizeof(cmdBuf) - 1) {
                cmdBuf[cmdPos++] = c;
            } else {
                discarding = true;  // FW-6: skip rest of this line
                cmdPos = 0;
            }
        }
    }

    // 3. Send real sensor data to B every DATA_INTERVAL (only while MEASURING)
    if (now - prevData >= DATA_INTERVAL) {
        prevData = now;
        if (isMeasuring()) sendDataToB();
    }
 
    // 4. Serial (→ rttDebug via macro) debug commands — RTT terminal input.
    if (Serial.available() > 0) {
        char c = Serial.read();
        while (Serial.available()) Serial.read();   // flush rest
        switch (c) {
            case '1': enterMeasuring(); Serial.println("[CMD] Force MEASURING"); break;
            case '2': enterIdle();      Serial.println("[CMD] Force IDLE");      break;
            case '3': printdata();                                                                          break;
            case '4': requestAlertFromB();                                                                  break;
            case '5': sendDataToB();        Serial.println("[CMD] Real DATA sent.");                        break;
            case '6': sendFakeDataToB();    Serial.println("[CMD] Fake DATA sent.");                        break;
            default:  break;
        }
    }

    // 5. Piston auto-stop + state-machine transitions (CLAUDE.md rule 5)
    update_actuators();

    // 6. Button polling (PA7 open / PB0 close / PB12 toggle)
    check_PhysicalButtons();

    // 7. Periodic USB printout
    if (now - prevPrint >= PRINT_INTERVAL) {
        prevPrint = now;
        printdata();
    }

    // 8. Periodic alert via B (only while MEASURING and CO2 ready)
    if (isMeasuring() && g_co2Ready) {
        if (now - lastAlertMs >= ALERT_INTERVAL) {
            lastAlertMs = now;
            requestAlertFromB();
        }
    }
}