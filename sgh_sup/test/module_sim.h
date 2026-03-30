#ifndef MODULE_SIM_H
#define MODULE_SIM_H

// ============================================================
//  module_sim.h  —  STM32-B  Gateway node
//
//  Responsibilities:
//    1. Receive "DATA:<csv>" lines from STM32-A via USART2
//       and publish them to HiveMQ as JSON over MQTT.
//    2. Receive MQTT commands from the broker and forward
//       them to STM32-A as plain command strings.
//    3. Receive "ALERT:<phone>,<msg>" lines from STM32-A
//       and POST them to ntfy.sh (HTTP push notification).
//    4. Relay ACKs from STM32-A back to the broker.
//
//  PIN WIRING:
//    USART1  PA9  TX / PA10 RX  →  SIM A7680C
//    USART2  PA2  TX / PA3  RX  →  STM32-A  (inter-board link)
// ============================================================

#define TINY_GSM_MODEM_SIM7600    // A7680C is SIM7600-compatible
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoHttpClient.h>

// -------------------------------------------------------
//  SIM A7680C — USART1  (PA10 RX, PA9 TX)
//  Per hardware diagram: STM32-B PA9/PA10 → A7680C RX/TX
// -------------------------------------------------------
#define SIM_RX_PIN   PA3
#define SIM_TX_PIN   PA3
#define SIM_BAUD     115200

// -------------------------------------------------------
//  STM32-A link — USART2  (PA3 RX, PA2 TX)
//  Per hardware diagram:
//    STM32-B PA2 TX  →  STM32-A PA9  RX
//    STM32-B PA3 RX  ←  STM32-A PA10 TX
// -------------------------------------------------------
#define STM32A_RX_PIN  PA3
#define STM32A_TX_PIN  PA2
#define STM32A_BAUD    57600

// -------------------------------------------------------
//  APN — Vietnamobile
// -------------------------------------------------------
#define SIM_APN       "m3-world"
#define SIM_APN_USER  ""
#define SIM_APN_PASS  ""

// -------------------------------------------------------
//  MQTT — HiveMQ public broker
//  To use HiveMQ Cloud (TLS, auth), change port to 8883
//  and supply MQTT_USER / MQTT_PASS.
// -------------------------------------------------------
#define MQTT_BROKER     "broker.hivemq.com"
#define MQTT_PORT       8884
#define MQTT_USER       ""
#define MQTT_PASS       ""
#define MQTT_CLIENT_ID  "clientId-ssxG23t4Up"   // unique per device

#define MQTT_TOPIC_SENSORS  "greenhouse/stm32/sensors"   // B publishes here
#define MQTT_TOPIC_CONTROL  "greenhouse/stm32/control"   // B subscribes here
#define MQTT_TOPIC_ACK      "greenhouse/stm32/ack"       // B publishes ACKs

// -------------------------------------------------------
//  HTTP alert — ntfy.sh push notification
//  Topic is the last path segment; change it to something
//  private so strangers can't read your alerts.
//  Subscribe on phone: install ntfy app, follow topic.
// -------------------------------------------------------
#define NTFY_HOST       "ntfy.sh"
#define NTFY_PORT       80
#define NTFY_TOPIC      "greenhouse_alert_abc123"   // change this

// -------------------------------------------------------
//  Timing
// -------------------------------------------------------
#define SIM_NETWORK_TIMEOUT_MS   60000UL
#define MQTT_RECONNECT_INTERVAL  10000UL
#define GPRS_CHECK_INTERVAL      300000UL
#define HTTP_TIMEOUT_MS          15000UL

// -------------------------------------------------------
//  Shared objects (used by main.cpp)
// -------------------------------------------------------
extern TinyGsm        modem;
extern TinyGsmClient  gsmClientMQTT;
extern PubSubClient   mqttClient;
extern HardwareSerial SerialA;   // link to STM32-A (USART2 PA3 RX / PA2 TX)

// ============================================================
//  Initialisation
// ============================================================
void setupSIM_A7680();

// ============================================================
//  Call every loop()
// ============================================================
void loopGateway();          // drives MQTT keepalive + GPRS watchdog

// ============================================================
//  Called from main when a DATA: line arrives from STM32-A
// ============================================================
void onDataFromA(const String& csvLine);

// ============================================================
//  Called from main when an ALERT: line arrives from STM32-A
// ============================================================
void onAlertFromA(const String& phone, const String& message);

// ============================================================
//  Internal — do not call directly
// ============================================================
void connectMQTT();
void maintainGPRS();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool sendHttpAlert(const String& message);

#endif // MODULE_SIM_H