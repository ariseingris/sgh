#ifndef MODULE_SIM_H
#define MODULE_SIM_H

// ============================================================
//  module_sim.h  —  STM32-B  Gateway node
// ============================================================

#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoHttpClient.h>

// BUG FIX: RTTSerial was duplicated here AND in rtt_debug.h.
// Including both headers in the same TU caused a duplicate class
// definition. RTTSerial now lives only in rtt_debug.h.
#include "rtt_debug.h"

// -------------------------------------------------------
//  Pin wiring (Blue Pill)
//    USART2  PA3(RX) / PA2(TX)  →  SIM A7680C
//    USART1  PA10(RX) / PA9(TX) →  STM32-A link
// -------------------------------------------------------

// SIM A7680C — USART2
#define SIM_RX_PIN   PA3
#define SIM_TX_PIN   PA2
#define SIM_BAUD     115200

// STM32-A link — USART1
#define STM32A_RX_PIN  PA10
#define STM32A_TX_PIN  PA9
#define STM32A_BAUD    115200

// -------------------------------------------------------
//  APN — Vietnamobile
// -------------------------------------------------------
#define SIM_APN       "m3-world"
#define SIM_APN_USER  ""
#define SIM_APN_PASS  ""

// -------------------------------------------------------
//  MQTT — HiveMQ public broker
// -------------------------------------------------------
#define MQTT_BROKER      "broker.hivemq.com"
#define MQTT_PORT        1883

// BUG FIX: empty-string user/pass causes PubSubClient to send a
// CONNECT packet with username flag set but zero-length username.
// Use nullptr macros to trigger the anonymous-connect overload.
#define MQTT_USER        nullptr
#define MQTT_PASS        nullptr

#define MQTT_CLIENT_ID   "clientId-ssxG23t4Up"
#define MQTT_KEEPALIVE   60   // seconds; default 15s is too tight for GPRS

#define MQTT_TOPIC_SENSORS  "greenhouse/stm32/sensors"
#define MQTT_TOPIC_CONTROL  "greenhouse/stm32/control"
#define MQTT_TOPIC_ACK      "greenhouse/stm32/ack"

// -------------------------------------------------------
//  HTTP alert — ntfy.sh
// -------------------------------------------------------
#define NTFY_HOST   "ntfy.sh"
#define NTFY_PORT   80
#define NTFY_TOPIC  "greenhouse_alert_abc123"   // change per deployment

// -------------------------------------------------------
//  Timing
// -------------------------------------------------------
#define SIM_NETWORK_TIMEOUT_MS   60000UL
#define MQTT_RECONNECT_INTERVAL  10000UL
#define GPRS_CHECK_INTERVAL      300000UL
#define HTTP_TIMEOUT_MS          15000UL

// -------------------------------------------------------
//  Shared objects (used by main.cpp on STM32-B)
// -------------------------------------------------------
extern TinyGsm        modem;
extern TinyGsmClient  gsmClientMQTT;
extern PubSubClient   mqttClient;
extern HardwareSerial SerialA;

// -------------------------------------------------------
//  Public API
// -------------------------------------------------------
void setupSIM_A7680();
void loopGateway();
void onDataFromA(const String& csvLine);
void onAlertFromA(const String& phone, const String& message);

// Internal — do not call directly from main
void connectMQTT();
void maintainGPRS();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool sendHttpAlert(const String& message);

#endif // MODULE_SIM_H