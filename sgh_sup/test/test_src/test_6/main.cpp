// ============================================================
//  TEST 6 — Full Pipeline: Mock STM32-A Data → HiveMQ
//
//  Goal: Simulate the complete data path STM32-A → STM32-B → HiveMQ
//        using a hardcoded (mock) sensor payload instead of reading
//        from USART1. STM32-A must NOT be connected.
//
//  Pre-requisite: Tests 1–5 all passed.
//
//  Steps:
//    1. Modem init + IMEI → build per-device MQTT topics
//    2. GPRS connect
//    3. TCP + MQTT connect (3 attempts, 2s stabilise, drain stale bytes)
//    4. Publish mock payload to  greenhouse/<devId>/sensors
//    5. Subscribe to            greenhouse/<devId>/control
//    6. Wait 5s and print any control messages received
//
//  Pass criteria:
//    • publish() returns true
//    • RTT shows "[MQTT] Published OK"
//    • mqtt.connected() still true after 5s wait
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

// Mock sensor payload — what STM32-A would normally send
static const char MOCK_PAYLOAD[] =
    "{\"temp\":28.5,\"hum\":72.3,\"co2\":625,\"lux\":480,\"pressure\":1013.2}";

static char mqttTopicSensors[64];
static char mqttTopicControl[64];
static bool controlMsgReceived = false;

static void onMqttMessage(char* topic, byte* payload, unsigned int len) {
    Serial.print("[MQTT←] Control on '");
    Serial.print(topic);
    Serial.print("': ");
    for (unsigned int i = 0; i < len; i++) Serial.print((char)payload[i]);
    Serial.println();
    controlMsgReceived = true;
}

static const char* mqttState(int s) {
    switch (s) {
        case  2: return "CONNECTED";
        case -1: return "DISCONNECTED";
        case -2: return "CONNECT_FAILED";
        case -3: return "CONNECTION_LOST";
        case -4: return "CONNECTION_TIMEOUT (port blocked or modem issue)";
        default: return "UNKNOWN";
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  TEST 6 — Full Pipeline (Mock STM32-A)");
    Serial.println("========================================");
    Serial.println("[INFO] STM32-A NOT connected — using hardcoded payload");
    Serial.println("[INFO] Mock payload:");
    Serial.println(MOCK_PAYLOAD);
    Serial.flush();

    pinMode(PC13, OUTPUT);
    digitalWrite(PC13, HIGH);

    // ======== LAYER 1/4 — Modem ==========================================
    Serial.println("\n[LAYER 1/4] Modem init...");
    Serial.flush();

    SerialSIM.begin(115200);
    delay(8000);   // cold-boot

    modem.init();
    if (!modem.testAT(5000)) {
        modem.restart();
        delay(10000);
        if (!modem.testAT(5000)) {
            Serial.println("[TEST 6][FAIL] LAYER 1 — Modem not responding");
            Serial.flush();
            return;
        }
    }

    String imei = modem.getIMEI();
    imei.trim();
    String devId = (imei.length() >= 8) ? imei.substring(imei.length() - 8) : "00000000";

    snprintf(mqttTopicSensors, sizeof(mqttTopicSensors),
             "sgh-aeris/gateway/%s/sensors", devId.c_str());
    snprintf(mqttTopicControl, sizeof(mqttTopicControl),
             "sgh-aeris/gateway/%s/control", devId.c_str());

    Serial.print("[LAYER 1/4] Modem OK. IMEI suffix: ");
    Serial.println(devId);
    Serial.print("[MQTT] sensors topic: ");
    Serial.println(mqttTopicSensors);
    Serial.print("[MQTT] control topic: ");
    Serial.println(mqttTopicControl);

    // ======== LAYER 2/4 — GPRS ===========================================
    Serial.println("\n[LAYER 2/4] GPRS connect...");
    Serial.flush();

    if (!modem.waitForNetwork(60000)) {
        Serial.println("[TEST 6][FAIL] LAYER 2 — No GSM network");
        Serial.flush();
        return;
    }

    bool gprsOk = false;
    for (int g = 1; g <= 3; g++) {
        Serial.print("[SIM] GPRS attempt ");
        Serial.print(g);
        Serial.println("/3...");
        Serial.flush();
        if (modem.gprsConnect(SIM_APN_NAME, SIM_APN_USER, SIM_APN_PASS)) {
            gprsOk = true;
            break;
        }
        if (g < 3) delay(5000);
    }
    if (!gprsOk) {
        Serial.println("[TEST 6][FAIL] LAYER 2 — GPRS connect failed");
        Serial.flush();
        return;
    }
    Serial.print("[LAYER 2/4] GPRS OK. IP: ");
    Serial.println(modem.getLocalIP());

    // ======== LAYER 3/4 — TCP + MQTT =====================================
    Serial.println("\n[LAYER 3/4] MQTT connect...");
    Serial.flush();

    String clientId = String("sgh_t6_") + devId;
    mqtt.setServer(MQTT_BROKER, MQTT_PORT);
    mqtt.setCallback(onMqttMessage);
    mqtt.setKeepAlive(60);
    mqtt.setSocketTimeout(30);
    mqtt.setBufferSize(512);

    bool mqttOk = false;
    for (int attempt = 1; attempt <= 3 && !mqttOk; attempt++) {
        Serial.print("[MQTT] Attempt ");
        Serial.print(attempt);
        Serial.print("/3: TCP → ");
        Serial.print(MQTT_BROKER);
        Serial.print(":");
        Serial.println(MQTT_PORT);
        Serial.flush();

        if (gsmClient.connected()) { gsmClient.stop(); delay(500); }

        if (!gsmClient.connect(MQTT_BROKER, MQTT_PORT)) {
            Serial.println("[MQTT] TCP FAILED");
            if (attempt < 3) { delay(5000); }
            continue;
        }
        Serial.println("[MQTT] TCP OK — stabilising 2s...");
        delay(2000);

        // Drain stale modem bytes so CONNACK is not confused
        uint32_t drained = 0;
        while (gsmClient.available()) { gsmClient.read(); drained++; }
        if (drained > 0) {
            Serial.print("[MQTT] Drained ");
            Serial.print(drained);
            Serial.println(" stale byte(s)");
        }

        Serial.print("[MQTT] CONNECT as '");
        Serial.print(clientId);
        Serial.print("'...");
        Serial.flush();

        if (mqtt.connect(clientId.c_str())) {
            mqttOk = true;
            Serial.println(" OK");
            mqtt.subscribe(mqttTopicControl);
            Serial.print("[MQTT] Subscribed to ");
            Serial.println(mqttTopicControl);
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

    if (!mqttOk) {
        Serial.println("[TEST 6][FAIL] LAYER 3 — MQTT connect failed");
        Serial.flush();
        return;
    }
    Serial.println("[LAYER 3/4] MQTT OK");

    // ======== LAYER 4/4 — Publish mock payload ===========================
    Serial.println("\n[LAYER 4/4] Publishing mock sensor payload...");
    Serial.print("[MQTT→] Topic:   ");
    Serial.println(mqttTopicSensors);
    Serial.print("[MQTT→] Payload: ");
    Serial.println(MOCK_PAYLOAD);
    Serial.flush();

    bool pubOk = mqtt.publish(mqttTopicSensors, MOCK_PAYLOAD);
    mqtt.loop();

    if (!pubOk) {
        Serial.println("[TEST 6][FAIL] LAYER 4 — publish() returned false");
        Serial.flush();
        return;
    }
    Serial.println("[MQTT] Published OK");

    // ---- Wait 5s for incoming control messages --------------------------
    Serial.println("[MQTT] Waiting 5s for control messages on subscribe topic...");
    Serial.flush();
    unsigned long t0 = millis();
    while (millis() - t0 < 5000) {
        mqtt.loop();
        delay(100);
    }

    if (!controlMsgReceived) {
        Serial.println("[MQTT] No control message received (normal — nobody published one)");
    }

    // ---- Summary ---------------------------------------------------------
    Serial.println("\n----------------------------------------");
    if (pubOk && mqtt.connected()) {
        Serial.println("[TEST 6][PASS] All 4 layers OK:");
        Serial.println("  LAYER 1 Modem    — PASS");
        Serial.println("  LAYER 2 GPRS     — PASS");
        Serial.println("  LAYER 3 MQTT     — PASS");
        Serial.println("  LAYER 4 Publish  — PASS");
        Serial.println("[INFO] Verify with: mosquitto_sub -h broker.hivemq.com -p 1883 -t 'sgh-aeris/gateway/#' -v");
    } else {
        Serial.println("[TEST 6][FAIL] Connection lost after publish");
        Serial.print("[MQTT] state=");
        Serial.println(mqttState(mqtt.state()));
    }
    Serial.println("========================================");
    Serial.flush();

    mqtt.disconnect();
    gsmClient.stop();
}

void loop() {
    // Keep MQTT alive so we can still receive control messages
    if (mqtt.connected()) mqtt.loop();

    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        digitalWrite(PC13, !digitalRead(PC13));
    }
}
