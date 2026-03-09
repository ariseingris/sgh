#ifndef SIM_MODUN_H
#define SIM_MODUN_H

// ============================================================
//  SIM A7680C - Communication Module Header
//  TinyGSM + PubSubClient (MQTT via HiveMQ broker)
//  Board: STM32 Blue Pill (bluepill_f103c8)
//
//  PIN WIRING (USART1):
//    STM32 PA9  = USART1 TX  →  SIM module RX
//    STM32 PA10 = USART1 RX  ←  SIM module TX
// ============================================================

#define TINY_GSM_MODEM_SIM7600   // A7680C is SIM7600-compatible
#define TINY_GSM_RX_BUFFER 1024  // Needed for MQTT payloads; default 64 is too small

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>

// -------------------------------------------------------
//  SIM Serial pins - STM32 Blue Pill USART1
//  HardwareSerial constructor is (RX, TX)
//  PA10 = USART1 RX (receives from SIM TX)
//  PA9  = USART1 TX (sends to SIM RX)
// -------------------------------------------------------
#define SIM_RX_PIN   PA10
#define SIM_TX_PIN   PA9
#define SIM_BAUD     115200

// -------------------------------------------------------
//  APN - Vietnamobile
// -------------------------------------------------------
#define SIM_APN      "m3-world"
#define SIM_APN_USER ""
#define SIM_APN_PASS ""

// -------------------------------------------------------
//  MQTT - HiveMQ Public Broker (no account needed)
//  NOTE: Public broker - do not send private data
//  Change MQTT_CLIENT_ID to something unique if you have
//  multiple devices, otherwise the broker will kick one off
// -------------------------------------------------------
#define MQTT_BROKER    "broker.hivemq.com"
#define MQTT_PORT      1883
#define MQTT_USER      ""
#define MQTT_PASS      ""
#define MQTT_CLIENT_ID "STM32_Greenhouse_A7680C"  // Must be unique per device

// Topics - subscribe to these in HiveMQ Websocket client to monitor
#define MQTT_TOPIC_SENSORS  "greenhouse/stm32/sensors"  // STM32 publishes here
#define MQTT_TOPIC_CONTROL  "greenhouse/stm32/control"  // Web/dashboard sends commands here

// -------------------------------------------------------
//  Timing
// -------------------------------------------------------
#define SIM_NETWORK_TIMEOUT_MS   60000UL   // 60s to find GSM network
#define SMS_SEND_TIMEOUT_MS      15000UL   // 15s for SMS response
#define MQTT_RECONNECT_INTERVAL  10000UL   // Retry MQTT every 10s if disconnected
#define GPRS_CHECK_INTERVAL      300000UL  // Check GPRS health every 5 min

// -------------------------------------------------------
//  Expose modem + clients so main.cpp can use if needed
// -------------------------------------------------------
extern TinyGsm       modem;
extern TinyGsmClient gsmClient;
extern PubSubClient  mqttClient;

// ============================================================
//  Core SIM / GPRS
// ============================================================
void setupSIM_A7680();
void clearSIMBuffer();
void maintainGPRS();        // Call periodically in loop()
void updateSIM_Connection();// Forward URC messages to Serial

// ============================================================
//  SMS
// ============================================================
void sendSMS_Alert(const String& phoneNumber, const String& message);

// ============================================================
//  MQTT
// ============================================================
void connectMQTT();

/**
 * Publish all sensor values as a JSON string to MQTT_TOPIC_SENSORS.
 * Monitor on HiveMQ Websocket: wss://www.hivemq.com/demos/websocket-client/
 */
void publishSensorMQTT(float temp, float hum, uint16_t co2,
                       float lux, float pressure, int gas, int soil);

/**
 * MUST be called every loop() to:
 *   1. Keep MQTT connection alive (sends PINGREQ)
 *   2. Process incoming messages on MQTT_TOPIC_CONTROL
 *      Commands received here control Fan / Piston
 */
void loopMQTT();

// Internal callback - do not call directly
void mqttCallback(char* topic, byte* payload, unsigned int length);

// ============================================================
//  [FUTURE PHASE 2] HTTP/HTTPS - uncomment when ready
//  Add to platformio.ini: arduino-libraries/ArduinoHttpClient
// ============================================================
// #include <ArduinoHttpClient.h>
// int    sendDataHTTP(const String& host, int port, const String& path, const String& payload);
// String fetchCommandHTTP(const String& host, int port, const String& path);

#endif // SIM_MODUN_H