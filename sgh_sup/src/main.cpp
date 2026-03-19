// ============================================================
//  main.cpp  —  STM32 Blue Pill (Sensor + Actuator node)
//
//  Changes from original:
//    - All SIM/MQTT/SMS calls REMOVED
//    - Sensor data is sent to Arduino Nano via USART1 (Serial1)
//      as "DATA:<csv>" lines every 10 seconds
//    - Commands received from Nano on the same UART are parsed
//      to drive Fan / Piston (same commands as before)
//    - SMS requests sent as "SMS:<phone>,<message>" lines
//
//  PIN WIRING (USART1 on Blue Pill):
//    PA9  (TX) → Nano D10 (SoftwareSerial RX)
//    PA10 (RX) ← Nano D11 (SoftwareSerial TX)
//    GND  shared between boards
//    Use a level-shifter if needed (STM32 is 3.3V, Nano is 5V)
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include "sensor_light.h"
#include "sensor_temp_hum.h"
#include "sensor_co2.h"
#include "sensor_pressure.h"
#include "sensor_gas.h"
#include "sensor_soil.h"
#include "gadget.h"

// -------------------------------------------------------
//  USART1 → Nano link
//  PA9=TX, PA10=RX  (same pins as the old SIM wiring)
// -------------------------------------------------------
HardwareSerial SerialNano(PA10, PA9);  // RX=PA10, TX=PA9
#define NANO_BAUD 57600

// -------------------------------------------------------
//  SMS config  (request sent to Nano to forward)
// -------------------------------------------------------
const String phoneNumber            = "+84812065252";
const unsigned long interval_message = 3600000UL;
unsigned long last_period_message    = 0;

// -------------------------------------------------------
//  System state
// -------------------------------------------------------
bool systemActive   = false;
bool actuator_state = false;

// -------------------------------------------------------
//  Timers
// -------------------------------------------------------
unsigned long previousSensorMillis  = 0;
const long    sensorInterval        = 5000;

unsigned long previousPrintMillis   = 0;
const long    printInterval         = 5000;

unsigned long previousDataSend      = 0;
const long    dataSendInterval      = 10000;  // Send DATA: to Nano every 10s

// -------------------------------------------------------
//  Global sensor values
// -------------------------------------------------------
float    g_lux          = 0.0;
float    g_temperature  = 0.0;
float    g_humidity     = 0.0;
float    g_pressure     = 0.0;
int      g_gasValue     = 0;
int      g_soilMoisture = 0;
uint16_t g_co2          = 0;
bool     g_dataReady    = false;

// ============================================================
//  sendDataToNano()
//  Format: DATA:<temp>,<hum>,<co2>,<lux>,<pressure>,<gas>,<soil>
// ============================================================
void sendDataToNano() {
    char buf[128];
    snprintf(buf, sizeof(buf),
        "DATA:%.1f,%.1f,%u,%.1f,%.1f,%d,%d",
        g_temperature, g_humidity, g_co2,
        g_lux, g_pressure, g_gasValue, g_soilMoisture
    );
    SerialNano.println(buf);
}

// ============================================================
//  requestSMSFromNano()
//  Asks Nano to send an SMS (Nano owns the SIM module).
//  Message body must NOT contain commas.
// ============================================================
void requestSMSFromNano() {
    String message = "=== Greenhouse Alert ===\n";
    message += "Temp: "     + String(g_temperature, 1) + " C\n";
    message += "Hum: "      + String(g_humidity, 1)    + " %\n";
    message += "Gas: "      + String(g_gasValue)        + "\n";
    message += "Soil: "     + String(g_soilMoisture)    + "\n";
    message += "Light: "    + String(g_lux, 1)          + " Lux\n";
    message += "Pressure: " + String(g_pressure, 1)     + " hPa\n";
    message += g_dataReady
               ? "CO2: " + String(g_co2) + " ppm"
               : "CO2: Not Ready";
    // Replace any commas in values (rare but safe)
    message.replace(",", ";");

    // Send request to Nano
    SerialNano.print("SMS:");
    SerialNano.print(phoneNumber);
    SerialNano.print(",");
    SerialNano.println(message);

    Serial.println("[MAIN] SMS request sent to Nano.");
}

// ============================================================
//  parseNanoCommand()
//  Handle commands forwarded from MQTT broker via Nano.
//  Same command set as the original mqttCallback().
// ============================================================
void parseNanoCommand(const String& cmd) {
    Serial.print("[NANO CMD] ");
    Serial.println(cmd);

    if (cmd == "SYSTEM_ON") {
        activate_system();
        systemActive = true;
        SerialNano.println("ACK:SYSTEM_ON");

    } else if (cmd == "SYSTEM_OFF") {
        deactivate_system();
        systemActive = false;
        SerialNano.println("ACK:SYSTEM_OFF");

    } else if (cmd == "FAN_ON") {
        turn_Fan_ON();
        SerialNano.println("ACK:FAN_ON");

    } else if (cmd == "FAN_OFF") {
        turn_Fan_OFF();
        SerialNano.println("ACK:FAN_OFF");

    } else if (cmd == "PISTON_OPEN") {
        extend_Piston();
        SerialNano.println("ACK:PISTON_OPEN");

    } else if (cmd == "PISTON_CLOSE") {
        retract_Piston();
        SerialNano.println("ACK:PISTON_CLOSE");

    } else if (cmd.startsWith("INFO:") || cmd.startsWith("ERR:") ||
               cmd.startsWith("URC:")) {
        // Status/error messages from Nano — just log them
        Serial.println("[NANO STATUS] " + cmd);

    } else {
        Serial.println("[NANO CMD] Unknown command, ignored.");
    }
}

// ============================================================
//  printdata()
// ============================================================
void printdata() {
    Serial.println("===== Du lieu cam bien =====");
    Serial.print("Nhiet do SHT30:  "); Serial.print(g_temperature); Serial.println(" C");
    Serial.print("Do am SHT30:     "); Serial.print(g_humidity);    Serial.println(" %");
    Serial.print("Ap suat BME280:  "); Serial.print(g_pressure);    Serial.println(" hPa");
    Serial.print("Anh sang BH1750: "); Serial.print(g_lux);         Serial.println(" Lux");
    Serial.print("Khi MQ-4:        "); Serial.println(g_gasValue);
    Serial.print("Do am dat:       "); Serial.print(g_soilMoisture); Serial.println(" (0-1023)");
    if (g_dataReady) {
        Serial.print("CO2 SCD40:       "); Serial.print(g_co2); Serial.println(" ppm");
    } else {
        Serial.println("CO2 SCD40:       Chua san sang");
    }
    Serial.println("============================");
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(2000);

    Wire.begin();
    Serial.println("====== KHOI TAO HE THONG (STM32 NODE) ======");

    setupBH1750_Sensor();
    setupSHT30_Sensor();
    setupSCD40_Sensor();
    setupBME280_Sensor();
    setup_Actuators();

    // Open UART link to Nano
    SerialNano.begin(NANO_BAUD);
    delay(500);
    Serial.println("[MAIN] UART Nano ready (PA9/PA10 @ 57600)");

    stop_Piston();

    Serial.println("====== HE THONG SAN SANG ======");
    Serial.println("Lenh Serial:");
    Serial.println("  1 = Kich hoat he thong");
    Serial.println("  2 = Tat he thong");
    Serial.println("  3 = In du lieu cam bien");
    Serial.println("  4 = Gui SMS (qua Nano)");
    Serial.println("  5 = Gui DATA ngay den Nano");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    unsigned long currentMillis = millis();

    // -------------------------------------------------------
    //  1. Read sensors every 5s
    // -------------------------------------------------------
    if (currentMillis - previousSensorMillis >= sensorInterval) {
        previousSensorMillis = currentMillis;

        g_lux          = readBH1750_Lux();
        readSHT30_Data(g_temperature, g_humidity);
        g_pressure     = readBME280_Pressure();
        g_gasValue     = readMQ4_Gas();
        g_soilMoisture = readSoil_Moisture();
        g_dataReady    = isSCD40_DataReady();
        if (g_dataReady) {
            g_co2 = readSCD40_CO2();
        }
    }

    // -------------------------------------------------------
    //  2. Read incoming lines from Nano (commands / status)
    // -------------------------------------------------------
    while (SerialNano.available()) {
        String line = SerialNano.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            parseNanoCommand(line);
        }
    }

    // -------------------------------------------------------
    //  3. Send sensor data to Nano every 10s
    // -------------------------------------------------------
    if (currentMillis - previousDataSend >= dataSendInterval) {
        previousDataSend = currentMillis;
        sendDataToNano();
    }

    // -------------------------------------------------------
    //  4. Serial commands (USB debug, same as original)
    // -------------------------------------------------------
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        delay(10);
        while (Serial.available()) Serial.read();

        switch (cmd) {
            case '1':
                systemActive = true;
                activate_system();
                Serial.println("[CMD] He thong KICH HOAT");
                break;
            case '2':
                systemActive = false;
                deactivate_system();
                Serial.println("[CMD] He thong TAT");
                break;
            case '3':
                printdata();
                break;
            case '4':
                requestSMSFromNano();
                break;
            case '5':
                sendDataToNano();
                Serial.println("[CMD] DATA sent to Nano.");
                break;
            default:
                break;
        }
    }

    // -------------------------------------------------------
    //  5. Actuator state sync
    // -------------------------------------------------------
    if (systemActive != actuator_state) {
        systemActive ? activate_system() : deactivate_system();
        actuator_state = systemActive;
    }

    // -------------------------------------------------------
    //  6. Print to USB Serial periodically
    // -------------------------------------------------------
    if (currentMillis - previousPrintMillis >= printInterval) {
        previousPrintMillis = currentMillis;
        printdata();
    }

    // -------------------------------------------------------
    //  7. Actuator non-blocking timer (auto-stop piston)
    // -------------------------------------------------------
    update_actuators();

    // -------------------------------------------------------
    //  8. Periodic SMS via Nano
    // -------------------------------------------------------
    if (systemActive && g_dataReady) {
        if (currentMillis - last_period_message >= interval_message) {
            requestSMSFromNano();
            last_period_message = currentMillis;
        }
    }
}