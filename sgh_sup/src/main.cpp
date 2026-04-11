// ============================================================
//  main.cpp  —  STM32-B  Minimal HiveMQ Test
//
//  Goal: Boot SIM A7680C → GPRS → MQTT → publish "HI"
//
//  Wiring:
//    USART1  PA10 RX / PA9 TX  →  SIM A7680C
// ============================================================

#include <Arduino.h>

#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024
#include <TinyGsmClient.h>
#include <PubSubClient.h>

// RTT debug output (Serial → ST-Link SWD, no UART needed)
#include "rtt_debug.h"
RTTSerial rttDebug;
#define Serial rttDebug

// -------------------------------------------------------
//  SIM A7680C on USART1  (PA10 RX, PA9 TX)
// -------------------------------------------------------
static HardwareSerial SerialSIM(PA10, PA9);
TinyGsm        modem(SerialSIM);
TinyGsmClient  gsmClient(modem, 0);
PubSubClient   mqtt(gsmClient);

// -------------------------------------------------------
//  Settings
// -------------------------------------------------------
#define SIM_APN      "m3-world"
#define MQTT_HOST    "broker.hivemq.com"
#define MQTT_PORT    1883
#define MQTT_ID      "clientId-ssxG23t4Up"
#define MQTT_TOPIC   "greenhouse/stm32/sensors"

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("=== STM32-B HiveMQ Test ===");

    // ── Init modem ──────────────────────────────────────────
    SerialSIM.begin(115200);
    Serial.println("[SIM] Cold-boot wait 8s...");
    delay(8000);

    Serial.println("[SIM] Probing modem...");
    if (!modem.testAT(3000)) {
        Serial.println("[SIM] No response — restarting...");
        modem.restart();
        delay(10000);
        if (!modem.testAT(5000)) {
            Serial.println("[SIM] ERROR: Modem not responding. Check PA9/PA10 wiring.");
            return;
        }
    }
    Serial.println("[SIM] Modem OK");

    // ── Wait for GSM network ────────────────────────────────
    Serial.println("[SIM] Waiting for GSM network (max 60s)...");
    if (!modem.waitForNetwork(60000)) {
        Serial.println("[SIM] ERROR: No GSM network.");
        return;
    }
    Serial.print("[SIM] GSM OK. Signal: ");
    Serial.println(modem.getSignalQuality());

    // ── Connect GPRS ────────────────────────────────────────
    Serial.println("[SIM] Connecting GPRS (APN=m3-world)...");
    if (!modem.gprsConnect(SIM_APN, "", "")) {
        Serial.println("[SIM] ERROR: GPRS failed.");
        return;
    }
    Serial.print("[SIM] GPRS OK. IP: ");
    Serial.println(modem.getLocalIP());

    // ── Connect MQTT ────────────────────────────────────────
    mqtt.setServer(MQTT_HOST, MQTT_PORT);
    Serial.print("[MQTT] Connecting to ");
    Serial.print(MQTT_HOST);
    Serial.println("...");

    if (!mqtt.connect(MQTT_ID)) {
        Serial.print("[MQTT] ERROR: Connect failed, state=");
        Serial.println(mqtt.state());
        return;
    }
    Serial.println("[MQTT] Connected!");

    // ── Publish "HI" ────────────────────────────────────────
    if (mqtt.publish(MQTT_TOPIC, "HI")) {
        Serial.println("[MQTT] Sent: HI  ->  " MQTT_TOPIC);
    } else {
        Serial.println("[MQTT] ERROR: Publish failed.");
    }
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    mqtt.loop();   // keepalive
}
