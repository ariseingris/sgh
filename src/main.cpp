#include <Arduino.h>
#include <Wire.h>
#include "sensor_light.h"    // BH1750
#include "sensor_temp_hum.h" // SHT31
#include "sensor_co2.h"      // SCD40
#include "sensor_pressure.h" // BME280
#include "sensor_gas.h"      // MQ-4 (Analog)
#include "sensor_soil.h"     // Soil Moisture (Analog)
#include "sim_modun.h"       // SIM A7680C + MQTT
#include "gadget.h"          // Actuators: Fan, Piston

// -------------------------------------------------------
//  SMS config
// -------------------------------------------------------
const String phoneNumber       = "+84812065252";
const unsigned long interval_message = 3600000UL; // SMS every 1 hour
unsigned long last_period_message    = 0;
bool alertSent = false;

// -------------------------------------------------------
//  System state
// -------------------------------------------------------
bool systemActive   = false;
bool actuator_state = false;

// -------------------------------------------------------
//  Timers
// -------------------------------------------------------
unsigned long previousSensorMillis  = 0;
const long    sensorInterval        = 5000;   // Read sensors every 5s

unsigned long previousPrintMillis   = 0;
const long    printInterval         = 5000;   // Print to Serial every 5s

unsigned long previousMqttPublish   = 0;
const long    mqttPublishInterval   = 10000;  // Publish to MQTT every 10s

unsigned long previousGprsCheck     = 0;
// GPRS_CHECK_INTERVAL defined in sim_modun.h (300000ms = 5 min)

// -------------------------------------------------------
//  Global sensor values (written by sensor timer, read by MQTT/SMS/print)
// -------------------------------------------------------
float    g_lux         = 0.0;
float    g_temperature = 0.0;
float    g_humidity    = 0.0;
float    g_pressure    = 0.0;
int      g_gasValue    = 0;
int      g_soilMoisture= 0;
uint16_t g_co2         = 0;
bool     g_dataReady   = false;

// ============================================================
//  printdata() - Serial monitor output
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
    Serial.print("MQTT connected:  "); Serial.println(mqttClient.connected() ? "YES" : "NO");
    Serial.println("============================");
}

// ============================================================
//  sms_sent_callback()
// ============================================================
void sms_sent_callback() {
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

    sendSMS_Alert(phoneNumber, message);
    Serial.println("[MAIN] SMS da gui.");
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(2000);

    Wire.begin();
    Serial.println("====== KHOI TAO HE THONG ======");

    setupBH1750_Sensor();
    setupSHT30_Sensor();
    setupSCD40_Sensor();
    setupBME280_Sensor();
    setup_Actuators();

    // SIM setup: wake modem → GSM network → GPRS → MQTT connect
    setupSIM_A7680();

    stop_Piston();

    Serial.println("====== HE THONG SAN SANG ======");
    Serial.println("Lenh Serial:");
    Serial.println("  1 = Kich hoat he thong");
    Serial.println("  2 = Tat he thong");
    Serial.println("  3 = In du lieu cam bien");
    Serial.println("  4 = Gui SMS ngay");
    Serial.println("  5 = Publish MQTT ngay");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    unsigned long currentMillis = millis();

    // -------------------------------------------------------
    //  1. Read sensors on interval (prevents I2C bus lockup)
    // -------------------------------------------------------
    if (currentMillis - previousSensorMillis >= sensorInterval) {
        previousSensorMillis = currentMillis;

        g_lux         = readBH1750_Lux();
        readSHT30_Data(g_temperature, g_humidity);
        g_pressure    = readBME280_Pressure();
        g_gasValue    = readMQ4_Gas();
        g_soilMoisture= readSoil_Moisture();
        g_dataReady   = isSCD40_DataReady();
        if (g_dataReady) {
            g_co2 = readSCD40_CO2();
        }
    }

    // -------------------------------------------------------
    //  2. MQTT keepalive + incoming command processing
    //     MUST run every loop() — do not put inside a timer
    // -------------------------------------------------------
    loopMQTT();

    // -------------------------------------------------------
    //  3. GPRS health check (every 5 minutes)
    // -------------------------------------------------------
    if (currentMillis - previousGprsCheck >= GPRS_CHECK_INTERVAL) {
        previousGprsCheck = currentMillis;
        maintainGPRS();
    }

    // -------------------------------------------------------
    //  4. Publish sensor data to MQTT every 10 seconds
    // -------------------------------------------------------
    if (currentMillis - previousMqttPublish >= mqttPublishInterval) {
        previousMqttPublish = currentMillis;
        publishSensorMQTT(g_temperature, g_humidity, g_co2,
                          g_lux, g_pressure, g_gasValue, g_soilMoisture);
    }

    // -------------------------------------------------------
    //  5. Serial commands (single char, instant)
    // -------------------------------------------------------
    if (Serial.available() > 0) {
        char cmd = Serial.read();
        delay(10);
        while (Serial.available()) Serial.read(); // flush \r\n

        switch (cmd) {
            case '1':
                systemActive = true;
                Serial.println("[CMD] He thong KICH HOAT");
                break;
            case '2':
                systemActive = false;
                Serial.println("[CMD] He thong TAT");
                break;
            case '3':
                printdata();
                break;
            case '4':
                sms_sent_callback();
                break;
            case '5':
                // Manual MQTT publish for testing
                publishSensorMQTT(g_temperature, g_humidity, g_co2,
                                  g_lux, g_pressure, g_gasValue, g_soilMoisture);
                Serial.println("[CMD] MQTT publish thu cong.");
                break;
            default:
                break;
        }
    }

    // -------------------------------------------------------
    //  6. Actuator state sync (only triggers on change)
    // -------------------------------------------------------
    if (systemActive != actuator_state) {
        systemActive ? activate_system() : deactivate_system();
        actuator_state = systemActive;
    }

    // -------------------------------------------------------
    //  7. Print to Serial periodically
    // -------------------------------------------------------
    if (currentMillis - previousPrintMillis >= printInterval) {
        previousPrintMillis = currentMillis;
        printdata();
    }

    // -------------------------------------------------------
    //  8. Actuator non-blocking timer (auto-stop piston)
    // -------------------------------------------------------
    update_actuators();

    // -------------------------------------------------------
    //  9. Periodic SMS (only when system active + CO2 ready)
    // -------------------------------------------------------
    if (systemActive && g_dataReady) {
        if (currentMillis - last_period_message >= interval_message) {
            sms_sent_callback();
            last_period_message = currentMillis;
            alertSent = true;
        }
    } else {
        alertSent = false;
    }

    // -------------------------------------------------------
    //  10. Forward any unsolicited modem messages to Serial
    // -------------------------------------------------------
    updateSIM_Connection();
}