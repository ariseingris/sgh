// ============================================================
//  TEST 4 — MQTT Connect + Publish
//
//  Goal: Complete MQTT handshake with HiveMQ public broker and
//        publish one test message to "sgh/test/ping".
//
//  Pre-requisite: TEST 3 passed (TCP reachable on a known port).
//
//  Pass criteria:
//    • mqtt.connected() returns true after connect()
//    • mqtt.publish("sgh/test/ping", ...) returns true
//
//  Failure state codes:
//    -4  MQTT_CONNECTION_TIMEOUT  — broker unreachable / port blocked
//    -2  MQTT_CONNECT_FAILED      — broker refused (bad credentials)
//    -1  MQTT_DISCONNECTED        — lost after connect
//
//  Output: RTT channel 0
// ============================================================

#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include <PubSubClient.h>
#include "SEGGER_RTT.h"
#include "config.h"    // SIM_APN_*, MQTT_BROKER, MQTT_PORT

// ---- Minimal RTTSerial -------------------------------------------------
class RTTSerial : public Stream {
public:
    void begin(unsigned long = 0) { SEGGER_RTT_Init(); }
    size_t write(uint8_t c) override { SEGGER_RTT_Write(0, &c, 1); return 1; }
    size_t write(const uint8_t* b, size_t s) override { SEGGER_RTT_Write(0, b, s); return s; }
    void flush() override { while (SEGGER_RTT_HasDataUp(0)) {} }
    int available() override { return 0; }
    int read()      override { return -1; }
    int peek()      override { return -1; }
};

RTTSerial rttDebug;
#define Serial rttDebug

static HardwareSerial SerialSIM(PA3, PA2);
TinyGsm      modem(SerialSIM);
TinyGsmClient gsmClient(modem, 0);
PubSubClient  mqtt(gsmClient);

// Human-readable PubSubClient state codes
static const char* mqttState(int s) {
    switch (s) {
        case  2: return "CONNECTED";
        case  1: return "BAD_PROTOCOL";
        case  0: return "DISCONNECTED (never connected)";
        case -1: return "DISCONNECTED (was connected, now lost)";
        case -2: return "CONNECT_FAILED (broker refused)";
        case -3: return "CONNECTION_LOST";
        case -4: return "CONNECTION_TIMEOUT — broker unreachable or port blocked";
        default: return "UNKNOWN";
    }
}

static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    Serial.print("[MQTT←] ");
    Serial.print(topic);
    Serial.print(": ");
    for (unsigned int i = 0; i < len; i++) Serial.print((char)payload[i]);
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  TEST 4 — MQTT Connect + Publish");
    Serial.println("========================================");
    Serial.flush();

    pinMode(PC13, OUTPUT);
    digitalWrite(PC13, HIGH);

    SerialSIM.begin(115200);
    Serial.println("[SIM] Cold-boot wait 8s...");
    Serial.flush();
    delay(8000);

    // ---- Modem ----------------------------------------------------------
    Serial.println("[SIM] Modem init...");
    modem.init();
    if (!modem.testAT(5000)) {
        modem.restart();
        delay(10000);
        if (!modem.testAT(5000)) {
            Serial.println("[TEST 4][FAIL] Modem not responding");
            Serial.flush();
            return;
        }
    }
    Serial.print("[SIM] Modem: ");
    Serial.println(modem.getModemInfo());

    // Read IMEI and build unique client ID
    String imei = modem.getIMEI();
    imei.trim();
    String clientId = String("sgh_test_");
    clientId += (imei.length() >= 8) ? imei.substring(imei.length() - 8) : "00000000";
    Serial.print("[MQTT] Client ID: ");
    Serial.println(clientId);

    // ---- GPRS -----------------------------------------------------------
    Serial.println("[SIM] Waiting for GSM network...");
    Serial.flush();
    if (!modem.waitForNetwork(60000)) {
        Serial.println("[TEST 4][FAIL] No GSM network");
        Serial.flush();
        return;
    }
    Serial.print("[SIM] Connecting GPRS APN='");
    Serial.print(SIM_APN_NAME);
    Serial.println("'...");
    Serial.flush();
    if (!modem.gprsConnect(SIM_APN_NAME, SIM_APN_USER, SIM_APN_PASS)) {
        Serial.println("[TEST 4][FAIL] GPRS failed");
        Serial.flush();
        return;
    }
    Serial.print("[SIM] GPRS OK. IP: ");
    Serial.println(modem.getLocalIP());

    // ---- Configure PubSubClient -----------------------------------------
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(onMqttMessage);
    mqtt.setKeepAlive(60);       // 60 s — safe for GPRS round-trips
    mqtt.setSocketTimeout(30);   // 30 s read timeout (default 15 too tight)
    mqtt.setBufferSize(512);

    // ---- TCP + MQTT connect loop (3 attempts) ---------------------------
    bool connected = false;
    for (int attempt = 1; attempt <= 3 && !connected; attempt++) {
        Serial.print("[MQTT] Attempt ");
        Serial.print(attempt);
        Serial.print("/3: TCP → ");
        Serial.print(MQTT_BROKER);
        Serial.print(":");
        Serial.println(MQTT_PORT);
        Serial.flush();

        // Release any stale socket (fix for state=-4 on re-attempt)
        if (gsmClient.connected()) { gsmClient.stop(); delay(500); }

        if (!gsmClient.connect(MQTT_BROKER, MQTT_PORT)) {
            Serial.println("[MQTT] TCP FAILED — broker unreachable or port blocked");
            if (attempt < 3) { Serial.println("[MQTT] Retry in 5s..."); delay(5000); }
            continue;
        }
        Serial.println("[MQTT] TCP OK");

        // KEY: A7680C needs ~2s after TCP connect before channel is stable
        Serial.println("[MQTT] Stabilising channel (2s)...");
        delay(2000);

        // Drain any AT response bytes the modem queued during TCP setup
        uint32_t drained = 0;
        while (gsmClient.available()) { gsmClient.read(); drained++; }
        if (drained > 0) {
            Serial.print("[MQTT] Drained ");
            Serial.print(drained);
            Serial.println(" stale modem byte(s)");
        }

        Serial.print("[MQTT] CONNECT as '");
        Serial.print(clientId);
        Serial.print("'...");
        Serial.flush();

        bool ok = mqtt.connect(clientId.c_str());
        if (ok) {
            connected = true;
            Serial.println(" OK");
        } else {
            int st = mqtt.state();
            Serial.print(" FAILED state=");
            Serial.print(st);
            Serial.print(" (");
            Serial.print(mqttState(st));
            Serial.println(")");
            gsmClient.stop();
            if (attempt < 3) { Serial.println("[MQTT] Retry in 5s..."); delay(5000); }
        }
    }

    if (!connected) {
        Serial.println("[TEST 4][FAIL] Could not connect to MQTT broker after 3 attempts");
        Serial.println("[HINT] If state=-4 on port 1883: run TEST 3, check if port 8883 works");
        Serial.flush();
        return;
    }

    // ---- Publish --------------------------------------------------------
    const char* topic   = "sgh/test/ping";
    const char* payload = "{\"test\":1,\"device\":\"SGH_SUP\"}";

    Serial.print("[MQTT→] Publish ");
    Serial.print(topic);
    Serial.print(" → ");
    Serial.println(payload);
    Serial.flush();

    bool pubOk = mqtt.publish(topic, payload);
    mqtt.loop();   // give PubSubClient a tick to flush

    // ---- Result ---------------------------------------------------------
    Serial.println("----------------------------------------");
    if (pubOk && mqtt.connected()) {
        Serial.println("[TEST 4][PASS] MQTT connected and message published");
        Serial.println("[INFO] Verify on PC: mosquitto_sub -h broker.hivemq.com -p 1883 -t 'sgh/#' -v");
    } else if (!pubOk) {
        Serial.print("[TEST 4][FAIL] publish() returned false. state=");
        Serial.println(mqtt.state());
    } else {
        Serial.println("[TEST 4][FAIL] mqtt.connected() false after publish");
    }
    Serial.println("========================================");
    Serial.flush();

    mqtt.disconnect();
    gsmClient.stop();
}

void loop() {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        digitalWrite(PC13, !digitalRead(PC13));
    }
}
