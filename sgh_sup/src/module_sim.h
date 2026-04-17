#ifndef MODULE_SIM_H
#define MODULE_SIM_H

// ============================================================
//  module_sim.h  —  STM32-B  Gateway node
//
//  Responsibilities:
//    1. Receive "DATA:<json>" lines from STM32-A via USART1
//       and publish the JSON payload verbatim to MQTT.
//    2. Receive MQTT commands from the broker and forward
//       them to STM32-A as plain command strings via UART.
//    3. Receive "ALERT:<msg>" lines from STM32-A
//       and POST them to ntfy.sh (HTTP push notification).
//    4. Relay ACKs from STM32-A back to the broker.
//
//  PIN WIRING (corrected):
//    USART1  PA9  TX / PA10 RX  →  STM32-A  (inter-board link)
//    USART2  PA2  TX / PA3  RX  →  SIM A7680C
// ============================================================

#define TINY_GSM_MODEM_SIM7600    // A7680C is SIM7600-compatible
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoHttpClient.h>
#include "config.h"

// -------------------------------------------------------
//  Pin definitions
// -------------------------------------------------------

// SIM A7680C  — USART2
#define SIM_RX_PIN   PA3
#define SIM_TX_PIN   PA2
#define SIM_BAUD     115200

// STM32-A link — USART1
#define STM32A_RX_PIN  PA10
#define STM32A_TX_PIN  PA9
#define STM32A_BAUD    115200

// -------------------------------------------------------
//  MQTT settings (broker host/port/user/pass from config.h)
// -------------------------------------------------------
#define MQTT_CLIENT_ID_PREFIX   "stm32_gw_aeris"
#define MQTT_KEEPALIVE          60

// Topic prefix — last 8 digits of IMEI appended at runtime.
#define MQTT_TOPIC_BASE     "sgh-aeris/gateway"

// -------------------------------------------------------
//  HTTP alert — ntfy.sh push notification
// -------------------------------------------------------
#define NTFY_HOST       "ntfy.sh"
#define NTFY_PORT       80

// -------------------------------------------------------
//  Timing
// -------------------------------------------------------
#define SIM_NETWORK_TIMEOUT_MS   60000UL
#define MQTT_RECONNECT_INTERVAL  10000UL
#define GPRS_CHECK_INTERVAL      300000UL
#define HTTP_TIMEOUT_MS          15000UL

// -------------------------------------------------------
//  Ring buffer for sensor payloads during MQTT outage.
// -------------------------------------------------------
#define SENSOR_BUFFER_SLOTS 10
#define SENSOR_PAYLOAD_LEN  256

// -------------------------------------------------------
//  Shared objects (minimal — only what main.cpp truly needs)
//  FLAW-4 FIX: modem, gsmClientMQTT, mqttClient are now internal.
//  Use the wrapper API below instead of direct access.
// -------------------------------------------------------
extern HardwareSerial SerialA;

// Per-device MQTT topics — built from IMEI at setupSIM_A7680() time
extern char mqttTopicSensors[64];
extern char mqttTopicControl[64];
extern char mqttTopicAck[64];
extern char mqttTopicHeartbeat[64];   // FLAW-5 FIX

// ============================================================
//  Initialisation
// ============================================================
void setupSIM_A7680();

// ============================================================
//  Call every loop()
// ============================================================
void loopGateway();

// ============================================================
//  FLAW-4 FIX: MQTT wrapper API.
//  main.cpp MUST NOT access mqttClient directly.
// ============================================================
bool isMqttConnected();
bool mqttPublish(const char* topic, const char* payload, bool retained = false);

// ============================================================
//  FLAW-6 FIX: Unified deferred publish.
//  Queues a message for publish in the next loop() iteration.
//  Call processDeferredAck() every loop().
// ============================================================
void processDeferredAck();
void queueAckPublish(const char* payload);

// ============================================================
//  Called from main when a DATA: line arrives from STM32-A.
//  FLAW-3 FIX: jsonPayload is the raw JSON string from A.
//  Published verbatim — no CSV parsing.
// ============================================================
void onDataFromA(const char* jsonPayload);

// ============================================================
//  Called from main when an ALERT: line arrives from STM32-A.
//  FLAW-2 FIX: phone number is now owned by STM32-B (config.h).
//  STM32-A sends just the message, not the phone number.
// ============================================================
void onAlertFromA(const String& message);

// ============================================================
//  Internal — do not call from main.cpp
// ============================================================
void connectMQTT();
void maintainGPRS();
void mqttCallback(char* topic, byte* payload, unsigned int length);
bool sendHttpAlert(const String& message);

// ============================================================
//  RTT Serial wrapper
//
//  BUG FIX #2 & #5: The original code put  #define Serial rttDebug
//  inside this shared header, which caused the macro to leak into
//  module_sim.cpp and corrupt TinyGSM's internal stream handling.
//
//  Fix: RTTSerial class is declared here (so main.cpp can use it)
//  but the  #define Serial rttDebug  macro is REMOVED from this
//  header. It is placed ONLY in main.cpp, AFTER all library
//  includes, so TinyGSM is never affected.
//
//  BUG FIX NEW: RTTSerial now extends Stream (not just Print).
//  Extending only Print means any call to Serial.available(),
//  Serial.read(), or Serial.peek() — even indirectly from a
//  library — either fails to compile or silently does nothing.
//  Stream provides these as pure virtuals; we provide safe stubs
//  (available=0, read=-1, peek=-1) since RTT output is write-only.
//
//  flush() is also added: SEGGER_RTT_Write() does not guarantee
//  the host has read the data before the MCU resets or faults.
//  flush() spins until the RTT up-buffer is drained so the last
//  debug lines are never lost on hard fault or watchdog reset.
// ============================================================
#include "SEGGER_RTT.h"

class RTTSerial : public Stream {
public:
    // ----- output (Print) -----
    size_t write(uint8_t c) override {
        SEGGER_RTT_Write(0, &c, 1);
        return 1;
    }

    size_t write(const uint8_t* buffer, size_t size) override {
        SEGGER_RTT_Write(0, buffer, size);
        return size;
    }

    // Spin until the host has consumed all bytes in the up-buffer.
    // Times out after 200 ms to avoid hanging if the host is disconnected.
    void flush() override {
        uint32_t t0 = millis();
        while (SEGGER_RTT_HasDataUp(0)) {
            if (millis() - t0 > 200) break; // 200 ms max
        }
    }

    // ----- input stubs (Stream) — RTT channel 0 is output-only -----
    int available() override { return 0; }
    int read()      override { return -1; }
    int peek()      override { return -1; }

    // Called from setup() — initialises the RTT control block.
    void begin(unsigned long /*baud*/ = 0) {
        SEGGER_RTT_Init();
    }
};

extern RTTSerial rttDebug;

// NOTE: Do NOT put  #define Serial rttDebug  here.
//       It lives only in main.cpp after all library includes.

#endif // MODULE_SIM_H
