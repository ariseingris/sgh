// ============================================================
//  main.cpp  —  STM32-B  Blue Pill  (Gateway node)
//
//  Role:
//    - Owns SIM A7680C (USART1: PA9/PA10)
//    - Owns UART link to STM32-A (USART2: PA2/PA3)
//    - Parses lines from STM32-A:
//        DATA:<csv>       → publish to MQTT
//        ALERT:<ph>,<msg> → HTTP POST to ntfy.sh
//        ACK:<cmd>        → publish to MQTT ack topic
//    - Receives MQTT commands from broker and forwards to A
//
//  No sensors, no actuators on this board.
// ============================================================

#include <Arduino.h>
#include "module_sim.h"

// -------------------------------------------------------
//  Timers
// -------------------------------------------------------
const unsigned long HEARTBEAT_INTERVAL = 30000UL;   
unsigned long lastHeartbeatMs = 0;

// ============================================================
//  parseLineFromA()
//  Dispatches a complete '\n'-terminated line received from A.
// ============================================================
static void parseLineFromA(const String& line) {
    Serial.print("[RX←A] ");
    Serial.println(line);

    if (line.startsWith("DATA:")) {
        // DATA:<temp>,<hum>,<co2>,<lux>,<pressure>,<gas>,<soil>
        onDataFromA(line.substring(5));

    } else if (line.startsWith("ALERT:")) {
        // ALERT:<phone>,<message>
        String rest  = line.substring(6);
        int    comma = rest.indexOf(',');
        if (comma > 0) {
            String phone = rest.substring(0, comma);
            String msg   = rest.substring(comma + 1);
            onAlertFromA(phone, msg);
        } else {
            Serial.println("[ALERT] Malformed ALERT line.");
        }

    } else if (line.startsWith("ACK:")) {
        // Forward ACK to MQTT broker for dashboard confirmation
        if (mqttClient.connected()) {
            char buf[80];
            snprintf(buf, sizeof(buf), "{\"ack\":\"%s\"}", line.c_str());
            mqttClient.publish(MQTT_TOPIC_ACK, buf);
        }

    } else {
        // Unknown — just log it
        Serial.println("[RX←A] Unrecognised line.");
    }
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("====== STM32-B GATEWAY INIT ======");
    setupSIM_A7680();
    Serial.println("====== GATEWAY READY ======");
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // 1. Read lines from STM32-A  (SerialA exposed by module_sim)
    while (SerialA.available()) {
        String line = SerialA.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) parseLineFromA(line);
    }

    // 2. Drive MQTT keepalive + GPRS watchdog
    loopGateway();

    // 3. Periodic heartbeat publish (lets dashboard know B is alive)
    unsigned long now = millis();
    if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL) {
        lastHeartbeatMs = now;
        if (mqttClient.connected()) {
            char buf[48];
            snprintf(buf, sizeof(buf), "{\"uptime\":%lu}", now / 1000UL);
            mqttClient.publish("greenhouse/stm32/heartbeat", buf);
        }
    }
}