// ============================================================
//  main.cpp  —  STM32-B  Blue Pill  (Gateway node)
//  DEBUG OUTPUT VIA SEGGER RTT (J-Link / ST-Link + OpenOCD)
//
//  LATENCY FIX L3: Non-blocking UART character accumulator.
//  FLAW-4 FIX: Uses wrapper API — never accesses mqttClient directly.
// ============================================================

#include <Arduino.h>
#include <IWatchdog.h>
#include "module_sim.h"

// ==========================================
// CỜ GIẢ LẬP DỮ LIỆU (MOCK SENSOR FLAG)
// Đặt là 1: Dùng data giả (bỏ qua STM32-A)
// Đặt là 0: Đọc data thật từ STM32-A
// ==========================================
#define USE_MOCK_DATA 0 

// BUG FIX #2 & #5:  #define Serial rttDebug  MUST come AFTER
// all library #includes, and MUST NOT be in module_sim.h.
#define Serial rttDebug

// RTTSerial instance (declared extern in module_sim.h)
RTTSerial rttDebug;

// -------------------------------------------------------
//  Timers
// -------------------------------------------------------
const unsigned long HEARTBEAT_INTERVAL  = 30000UL;
unsigned long       lastHeartbeatMs     = 0;

// -------------------------------------------------------
//  Non-blocking UART line buffer for STM32-A
// -------------------------------------------------------
static char   uartLineBuf[256];
static uint8_t uartLinePos = 0;

// ============================================================
//  parseLineFromA()
//
//  FLAW-3 FIX: DATA: payload is now raw JSON — forwarded verbatim.
//  FLAW-2 FIX: ALERT: payload is now just the message (no phone).
//  FLAW-4 FIX: Uses mqttPublish() wrapper, not mqttClient directly.
//  FLAW-6 FIX: ACK from STM32-A uses deferred queue.
// ============================================================
static void parseLineFromA(const String& line) {
    Serial.print("[RX←A] ");
    Serial.println(line);

    if (line.startsWith("DATA:")) {
        // FLAW-3: JSON payload — pass c_str directly to relay
        onDataFromA(line.c_str() + 5);  // skip "DATA:" prefix

    } else if (line.startsWith("ALERT:")) {
        // FLAW-2: message only (no phone), phone is in config.h
        String msg = line.substring(6);
        msg.trim();
        if (msg.length() > 0) {
            onAlertFromA(msg);
        }

    } else if (line.startsWith("ACK:")) {
        // FLAW-6: use deferred ACK queue instead of direct publish
        char buf[80];
        snprintf(buf, sizeof(buf), "{\"ack\":\"%s\"}", line.c_str());
        queueAckPublish(buf);

    } else if (line.startsWith("INFO:")) {
        // Status breadcrumb from STM32-A (e.g. INFO:BOOT) — already printed above

    } else {
        Serial.println("[RX←A] Unrecognised line.");
    }
}

// ============================================================
//  readSerialA_NonBlocking()
// ============================================================
static void readSerialA_NonBlocking() {
    while (SerialA.available()) {
        char c = (char)SerialA.read();

        if (c == '\n' || c == '\r') {
            if (uartLinePos > 0) {
                uartLineBuf[uartLinePos] = '\0';
                String line(uartLineBuf);
                line.trim();
                if (line.length() > 0) parseLineFromA(line);
                uartLinePos = 0;
            }
            continue;
        }

        if (uartLinePos < sizeof(uartLineBuf) - 1) {
            uartLineBuf[uartLinePos++] = c;
        } else {
            rttDebug.println("[UART] Line too long, discarding.");
            uartLinePos = 0;
        }
    }
}

// ============================================================
//  setup()
// ============================================================
void setup() {
    Serial.begin(115200);
    delay(1000);

    pinMode(PC13, OUTPUT);
    digitalWrite(PC13, HIGH);

    Serial.println("\n\n====== STM32-B GATEWAY INIT (RTT MODE) ======");
    Serial.println("Dang khoi dong module SIM...");
    Serial.println(" -> Kiem tra NGUON 2A hoac day RX/TX cua SIM");
    Serial.flush();

    setupSIM_A7680();

    Serial.println("====== GATEWAY READY ======");
    Serial.flush();

    // PHASE 5 FIX: Hardware watchdog — 30-second timeout.
    // Covers worst-case blocking: sendHttpAlert (15s) + maintainGPRS (10-30s).
    // A truly stuck TinyGSM AT-command hang will trigger a hard reset.
    IWatchdog.begin(30000000); // 30 seconds in microseconds
}

// ============================================================
//  loop()
// ============================================================
void loop() {
    IWatchdog.reload(); // PHASE 5 FIX: pet watchdog every loop iteration

    // 1. DUY TRÌ KẾT NỐI MẠNG (GPRS & MQTT)
    loopGateway();

    // 2. XỬ LÝ DỮ LIỆU (THẬT HOẶC GIẢ)
    unsigned long now = millis();

#if USE_MOCK_DATA == 1
    static unsigned long lastFakeDataMs = 0;
    if (now - lastFakeDataMs > 10000) {
        lastFakeDataMs = now;
        float fakeTemp = 28.0 + random(-10, 10) / 10.0;
        char fakeLine[128];
        // FLAW-3: mock data now sends JSON too
        snprintf(fakeLine, sizeof(fakeLine),
            "DATA:{\"temp\":%.1f,\"hum\":72.3,\"co2\":625,\"lux\":480.0,\"pressure\":1013.2,\"gas\":0,\"soil\":1}",
            fakeTemp);
        rttDebug.print("[MOCK] ");
        rttDebug.println(fakeLine);
        parseLineFromA(String(fakeLine));
    }
    while (SerialA.available()) { SerialA.read(); }

#else
    // LATENCY FIX L3: Non-blocking read
    readSerialA_NonBlocking();
#endif

    // 3. Process deferred ACKs (FLAW-6)
    processDeferredAck();

    // 4. NHÁY LED BÁO HIỆU LOOP ĐANG CHẠY
    static unsigned long lastBlink = 0;
    if (now - lastBlink > 500) {
        lastBlink = now;
        digitalWrite(PC13, !digitalRead(PC13));
    }

    // 5. HEARTBEAT MQTT
    //    FLAW-5 FIX: uses IMEI-based topic, not hardcoded "greenhouse/stm32/heartbeat"
    //    FLAW-4 FIX: uses mqttPublish() wrapper, not mqttClient directly
    if (now - lastHeartbeatMs >= HEARTBEAT_INTERVAL) {
        lastHeartbeatMs = now;
        if (isMqttConnected()) {
            char buf[48];
            snprintf(buf, sizeof(buf), "{\"uptime\":%lu}", now / 1000UL);
            mqttPublish(mqttTopicHeartbeat, buf);
            rttDebug.println("[MQTT] Sent Heartbeat");
        }
    }
}
