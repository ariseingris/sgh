#ifndef MODULE_SIM_H
#define MODULE_SIM_H

// ============================================================
//  module_sim.h  —  STM32-B  Gateway node
//
//  Responsibilities:
//    1. Receive "DATA:<csv>" lines from STM32-A via USART1
//       and publish them to HiveMQ as JSON over MQTT.
//    2. Receive MQTT commands from the broker and forward
//       them to STM32-A as plain command strings.
//    3. Receive "ALERT:<phone>,<msg>" lines from STM32-A
//       and POST them to ntfy.sh (HTTP push notification).
//    4. Relay ACKs from STM32-A back to the broker.
//
//  PIN WIRING (corrected):
//    USART1  PA9  TX / PA10 RX  →  STM32-A  (inter-board link)
//    USART2  PA2  TX / PA3  RX  →  SIM A7680C
//
//  Blue Pill hardware USART mapping:
//    USART1: PA9(TX)  PA10(RX)   ← used for STM32-A link
//    USART2: PA2(TX)  PA3(RX)    ← used for SIM A7680C
// ============================================================

#define TINY_GSM_MODEM_SIM7600    // A7680C is SIM7600-compatible
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include <ArduinoHttpClient.h>

// -------------------------------------------------------
//  BUG FIX #1: Pins were swapped in original code.
//
//  SIM A7680C  →  USART2  (PA3 RX, PA2 TX)
//  STM32-A     →  USART1  (PA10 RX, PA9 TX)
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
#define MQTT_USER        ""
#define MQTT_PASS        ""
// FIX 4: Hard-coded client ID causes broker conflicts when two
// gateways connect simultaneously — the broker kicks the first
// session offline. Replace with a macro placeholder; the actual
// unique ID is built at runtime from the modem IMEI in connectMQTT().
// Leave this as a fallback only (used if IMEI read fails).
#define MQTT_CLIENT_ID_PREFIX  "clientId-DnVcsTVDio"

// BUG FIX #4: Explicit keepalive (seconds). PubSubClient default
// is 15s which is too tight for GPRS round-trips. 60s is safe.
#define MQTT_KEEPALIVE   60

#define MQTT_TOPIC_SENSORS  "greenhouse/stm32/sensors"
#define MQTT_TOPIC_CONTROL  "greenhouse/stm32/control"
#define MQTT_TOPIC_ACK      "greenhouse/stm32/ack"

// -------------------------------------------------------
//  HTTP alert — ntfy.sh push notification
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
extern HardwareSerial SerialA;

// ============================================================
//  Initialisation
// ============================================================
void setupSIM_A7680();

// ============================================================
//  Call every loop()
// ============================================================
void loopGateway();

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
    // Safe to call from hard-fault handlers; uses only RTT API.
    void flush() override {
        while (SEGGER_RTT_HasDataUp(0)) {
            /* busy-wait */
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