// ============================================================
//  TEST 3 — TCP Connectivity to MQTT Broker
//
//  Goal: Confirm port 1883 (fallback 8883) is reachable from
//        Vietnamobile m-wap APN through to broker.hivemq.com.
//
//  Pre-requisite: TEST 2 passed (GPRS connected).
//
//  Pass criteria:
//    • TinyGsmClient.connect(broker, 1883) returns true, OR
//    • TinyGsmClient.connect(broker, 8883) returns true
//
//  If port 1883 fails but 8883 succeeds → set MQTT_PORT 8883 in config.h.
//  If both fail → carrier/APN firewall — try test.mosquitto.org.
//
//  Output: RTT channel 0
// ============================================================

#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include "SEGGER_RTT.h"
#include "config.h"    // SIM_APN_*, MQTT_BROKER

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
TinyGsm modem(SerialSIM);
TinyGsmClient client(modem, 0);   // TCP socket, channel 0

// Try connecting TCP, return the port that succeeded (0 = both failed)
static int tryTCP(const char* host, int port, unsigned long timeoutMs = 15000) {
    Serial.print("[TCP] Connecting ");
    Serial.print(host);
    Serial.print(":");
    Serial.print(port);
    Serial.print(" (");
    Serial.print(timeoutMs / 1000);
    Serial.println("s timeout)...");
    Serial.flush();

    if (client.connected()) { client.stop(); delay(500); }
    bool ok = client.connect(host, port, (int)(timeoutMs / 1000));
    if (ok) {
        Serial.print("[TCP] Port ");
        Serial.print(port);
        Serial.println(" OPEN");
        client.stop();
        return port;
    }
    Serial.print("[TCP] Port ");
    Serial.print(port);
    Serial.println(" FAILED (blocked or broker unreachable)");
    return 0;
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  TEST 3 — TCP Connectivity to Broker");
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
            Serial.println("[TEST 3][FAIL] Modem not responding — run TEST 1 first");
            Serial.flush();
            return;
        }
    }
    Serial.println("[SIM] Modem OK");

    // ---- GPRS -----------------------------------------------------------
    Serial.println("[SIM] Waiting for GSM network...");
    Serial.flush();
    if (!modem.waitForNetwork(60000)) {
        Serial.println("[TEST 3][FAIL] No GSM network — run TEST 2 first");
        Serial.flush();
        return;
    }
    Serial.print("[SIM] Connecting GPRS APN='");
    Serial.print(SIM_APN_NAME);
    Serial.println("'...");
    Serial.flush();
    if (!modem.gprsConnect(SIM_APN_NAME, SIM_APN_USER, SIM_APN_PASS)) {
        Serial.println("[TEST 3][FAIL] GPRS failed — run TEST 2 first");
        Serial.flush();
        return;
    }
    Serial.print("[SIM] GPRS OK. IP: ");
    Serial.println(modem.getLocalIP());

    // ---- TCP probe: port 1883 then 8883 ----------------------------------
    int successPort = tryTCP(MQTT_BROKER, 1883, 15000);

    if (successPort == 0) {
        Serial.println("[TCP] Port 1883 blocked — trying fallback 8883...");
        Serial.flush();
        delay(2000);
        successPort = tryTCP(MQTT_BROKER, 8883, 15000);
    }

    // ---- If both failed, try alternate broker ----------------------------
    if (successPort == 0) {
        Serial.println("[TCP] Both ports failed on " MQTT_BROKER);
        Serial.println("[TCP] Trying test.mosquitto.org:1883 as sanity check...");
        Serial.flush();
        delay(2000);
        int altPort = tryTCP("test.mosquitto.org", 1883, 15000);
        if (altPort > 0) {
            Serial.println("[TCP] test.mosquitto.org reachable — carrier blocks HiveMQ");
        } else {
            Serial.println("[TCP] Even test.mosquitto.org unreachable — APN may block all MQTT");
        }
    }

    // ---- Summary ----------------------------------------------------------
    Serial.println("----------------------------------------");
    if (successPort > 0) {
        Serial.print("[TEST 3][PASS] TCP connected on port ");
        Serial.println(successPort);
        if (successPort == 8883) {
            Serial.println("[ACTION] Carrier blocks port 1883.");
            Serial.println("[ACTION] Set #define MQTT_PORT 8883 in config.h before running TEST 4+");
        }
    } else {
        Serial.println("[TEST 3][FAIL] Both TCP ports failed");
        Serial.println("[HINT 1] Try alternate broker: test.mosquitto.org");
        Serial.println("[HINT 2] Check if SIM has active data plan");
        Serial.println("[HINT 3] Try different APN if m-wap does not give internet access");
    }
    Serial.println("========================================");
    Serial.flush();
}

void loop() {
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        digitalWrite(PC13, !digitalRead(PC13));
    }
}
