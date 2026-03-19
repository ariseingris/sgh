#ifndef MODULE_SIM_H
#define MODULE_SIM_H

// ============================================================
//  SIM A7680C - Communication Module Header
//  TinyGSM + PubSubClient (MQTT via HiveMQ broker)
//  Board: Arduino Nano
//
//  PIN WIRING:
//    Nano D0 (RX) ← SIM module TX   [Hardware Serial]
//    Nano D1 (TX) → SIM module RX   [Hardware Serial]
//
//  Nano D10 (RX2, SoftwareSerial) ← STM32 TX
//  Nano D11 (TX2, SoftwareSerial) → STM32 RX
//
//  NOTE: Because Nano has only ONE hardware UART, the USB
//  debug port (Serial) is shared with D0/D1.
//  During normal operation (SIM connected), do NOT use
//  Serial Monitor on Nano — it will corrupt SIM comms.
//  Use the STM32 Serial for debug prints instead.
// ============================================================

#define TINY_GSM_MODEM_SIM7600   // A7680C is SIM7600-compatible
#define TINY_GSM_RX_BUFFER 1024  // Needed for MQTT payloads

#include <Arduino.h>
#include <SoftwareSerial.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>

// -------------------------------------------------------
//  SIM A7680C — Hardware Serial (D0/D1) on Arduino Nano
//  "Serial" IS the hardware UART on Nano.
//  Baud must match modem firmware (default 115200).
//  WARNING: uploading new sketches requires SIM unplugged
//  or D0/D1 disconnected because bootloader uses same UART.
// -------------------------------------------------------
#define SIM_BAUD     115200

// -------------------------------------------------------
//  STM32 Link — SoftwareSerial on D10 (RX) / D11 (TX)
//  Keep baud ≤ 57600 for SoftwareSerial reliability on Nano.
//  Wire: Nano D10 ← STM32 TX,  Nano D11 → STM32 RX
//  Use a 3.3V↔5V level-shifter if STM32 GPIO is 3.3V only.
// -------------------------------------------------------
#define STM32_SW_RX  10   // Nano receives from STM32 TX
#define STM32_SW_TX  11   // Nano sends to STM32 RX
#define STM32_BAUD   57600

// -------------------------------------------------------
//  APN - Vietnamobile
// -------------------------------------------------------
#define SIM_APN      "m3-world"
#define SIM_APN_USER ""
#define SIM_APN_PASS ""

// -------------------------------------------------------
//  MQTT - HiveMQ Public Broker
// -------------------------------------------------------
#define MQTT_BROKER    "broker.hivemq.com"
#define MQTT_PORT      1883
#define MQTT_USER      ""
#define MQTT_PASS      ""
#define MQTT_CLIENT_ID "Nano_Greenhouse_A7680C"   // Unique per device

#define MQTT_TOPIC_SENSORS  "greenhouse/stm32/sensors"
#define MQTT_TOPIC_CONTROL  "greenhouse/stm32/control"

// -------------------------------------------------------
//  Timing
// -------------------------------------------------------
#define SIM_NETWORK_TIMEOUT_MS   60000UL
#define SMS_SEND_TIMEOUT_MS      15000UL
#define MQTT_RECONNECT_INTERVAL  10000UL
#define GPRS_CHECK_INTERVAL      300000UL

// -------------------------------------------------------
//  Expose objects so main.cpp can reference mqttClient
// -------------------------------------------------------
extern TinyGsm       modem;
extern TinyGsmClient gsmClient;
extern PubSubClient  mqttClient;
extern SoftwareSerial SerialSTM32;  // Link to STM32

// ============================================================
//  Core SIM / GPRS
// ============================================================
void setupSIM_A7680();
void clearSIMBuffer();
void maintainGPRS();
void updateSIM_Connection();

// ============================================================
//  SMS
// ============================================================
void sendSMS_Alert(const String& phoneNumber, const String& message);

// ============================================================
//  MQTT
// ============================================================
void connectMQTT();
void publishSensorMQTT(float temp, float hum, uint16_t co2,
                       float lux, float pressure, int gas, int soil);
void loopMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);

// ============================================================
//  STM32 link
// ============================================================

/**
 * Send a command string to the STM32 over SoftwareSerial.
 * STM32 must parse lines ending with '\n'.
 * Example: sendCommandToSTM32("FAN_ON");
 */
void sendCommandToSTM32(const String& cmd);

/**
 * Call every loop() — reads any status/ack lines the STM32
 * sends back (e.g. "ACK:FAN_ON") and prints them for debug.
 */
void readSTM32Response();

#endif // SIM_MODUN_H