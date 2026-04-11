// ============================================================
//  main.cpp  —  STM32-B  Blue Pill  (Gateway node)
//  DEBUG OUTPUT VIA SEGGER RTT (J-Link / ST-Link + OpenOCD)
// ============================================================

#include <Arduino.h>
#include "module_sim.h"

// BUG FIX #2 & #5:  #define Serial rttDebug  MUST come AFTER
// all library #includes, and MUST NOT be in module_sim.h.
// If placed in the header, TinyGSM's internal Serial references
// get macro-replaced, breaking AT command parsing entirely.
// Placing it here means only main.cpp code gets the redirect.
#define Serial rttDebug

// RTTSerial instance (declared extern in module_sim.h)
RTTSerial rttDebug;

// -------------------------------------------------------
//  Timers
// -------------------------------------------------------
const unsigned long HEARTBEAT_INTERVAL  = 30000UL;
unsigned long       lastHeartbeatMs     = 0;



// ============================================================
//  parseLineFromA()
// ============================================================
static void parseLineFromA(const String& line) {
    Serial.print("[RX←A] ");
    Serial.println(line);

    if (line.startsWith("DATA:")) {
        onDataFromA(line.substring(5));

    } else if (line.startsWith("ALERT:")) {
        int commaIdx = line.indexOf(',', 6);
        if (commaIdx > 6) {
            String phone = line.substring(6, commaIdx);
            String msg   = line.substring(commaIdx + 1);
            onAlertFromA(phone, msg);
        }

    } else if (line.startsWith("ACK:")) {
        if (mqttClient.connected()) {
            char buf[80];
            snprintf(buf, sizeof(buf), "{\"ack\":\"%s\"}", line.c_str());
            mqttClient.publish("greenhouse/stm32/ack", buf);
        }

    } else {
        Serial.println("[RX←A] Unrecognised line.");
    }
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);   // calls SEGGER_RTT_Init()
    delay(1000);

    pinMode(PC13, OUTPUT);
    digitalWrite(PC13, HIGH);   // LED off initially (PC13 active-low)

    Serial.println("\n\n====== STM32-B GATEWAY INIT (RTT MODE) ======");
    Serial.println("Dang khoi dong module SIM...");
    Serial.println(" -> Kiem tra NGUON 2A hoac day RX/TX cua SIM");

    // flush() ensures the above lines reach the host before the 8s
    // modem cold-boot delay swallows any RTT poll window.
    Serial.flush();

    setupSIM_A7680();

    Serial.println("====== GATEWAY READY ======");
    Serial.flush();
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    // 1. Read data from STM32-A (USART1: PA9 RX / PA10 TX)
    while (SerialA.available()) {
        String line = SerialA.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) parseLineFromA(line);
    }

    // 2. Maintain GPRS and MQTT
    loopGateway();

    unsigned long now = millis();

    // 3. Blink LED every 500ms to confirm loop() is running
    static unsigned long lastBlink = 0;
    if (now - lastBlink > 500) {
        lastBlink = now;
        digitalWrite(PC13, !digitalRead(PC13));
    }

    // 4. Send Heartbeat to HiveMQ
    if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL) {
        lastHeartbeatMs = now;
        if (mqttClient.connected()) {
            char buf[48];
            snprintf(buf, sizeof(buf), "{\"uptime\":%lu}", now / 1000UL);
            mqttClient.publish("greenhouse/stm32/heartbeat", buf);
            Serial.println("[MQTT] Sent Heartbeat");
        }
    }
}