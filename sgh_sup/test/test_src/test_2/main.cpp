// ============================================================
//  TEST 2 — GPRS / APN Connection
//
//  Goal: Confirm SIM A7680C can attach to GPRS on APN "m-wap"
//        (Vietnamobile) and receive an IP address.
//
//  Pre-requisite: TEST 1 passed (modem alive, SIM detected, signal OK).
//
//  Pass criteria:
//    • modem.waitForNetwork() returns true
//    • modem.gprsConnect()    returns true
//    • modem.getLocalIP()     is a valid non-zero address
//    • modem.isGprsConnected() returns true
//
//  Output: RTT channel 0
// ============================================================

#define TINY_GSM_MODEM_SIM7600    // A7680C is SIM7600-compatible
#define TINY_GSM_RX_BUFFER 1024

#include <Arduino.h>
#include <TinyGsmClient.h>
#include "SEGGER_RTT.h"
#include "config.h"   // SIM_APN_NAME, SIM_APN_USER, SIM_APN_PASS

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

static HardwareSerial SerialSIM(PA3, PA2);   // USART2
TinyGsm modem(SerialSIM);

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  TEST 2 — GPRS / APN Connection");
    Serial.println("========================================");
    Serial.flush();

    pinMode(PC13, OUTPUT);
    digitalWrite(PC13, HIGH);

    SerialSIM.begin(115200);
    Serial.println("[SIM] Cold-boot wait 8s...");
    Serial.flush();
    delay(8000);

    // ---- Modem init -------------------------------------------------------
    Serial.println("[SIM] modem.init()...");
    modem.init();

    if (!modem.testAT(5000)) {
        Serial.println("[SIM] No AT — restart...");
        modem.restart();
        Serial.println("[SIM] Post-restart wait 10s...");
        delay(10000);
        if (!modem.testAT(5000)) {
            Serial.println("[TEST 2][FAIL] Modem not responding. Run TEST 1 first.");
            Serial.flush();
            return;
        }
    }
    Serial.print("[SIM] Modem OK: ");
    Serial.println(modem.getModemInfo());

    // ---- Wait for GSM network registration --------------------------------
    Serial.println("[SIM] Waiting for GSM network (max 60s)...");
    Serial.flush();
    if (!modem.waitForNetwork(60000)) {
        Serial.println("[TEST 2][FAIL] No GSM network registration");
        Serial.println("[HINT] Check antenna, SIM card, and move to area with coverage");

        // Print +CREG to help diagnosis
        modem.sendAT(GF("+CREG?"));
        String creg;
        modem.waitResponse(3000, creg);
        Serial.print("[AT+CREG?] ");
        Serial.println(creg);
        Serial.flush();
        return;
    }
    Serial.print("[SIM] GSM registered. Signal quality: ");
    Serial.println(modem.getSignalQuality());

    // ---- GPRS connect -----------------------------------------------------
    Serial.print("[SIM] Connecting GPRS: APN='");
    Serial.print(SIM_APN_NAME);
    Serial.print("' user='");
    Serial.print(SIM_APN_USER);
    Serial.println("'...");
    Serial.flush();

    bool gprsOk = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.print("[SIM] GPRS attempt ");
        Serial.print(attempt);
        Serial.println("/3");
        Serial.flush();
        if (modem.gprsConnect(SIM_APN_NAME, SIM_APN_USER, SIM_APN_PASS)) {
            gprsOk = true;
            break;
        }
        if (attempt < 3) {
            Serial.println("[SIM] GPRS failed, retry in 5s...");
            delay(5000);
        }
    }

    if (!gprsOk) {
        Serial.println("[TEST 2][FAIL] gprsConnect() failed after 3 attempts");
        Serial.println("[HINT] Check APN string, SIM balance, data plan roaming");
        // Dump PDP context state for diagnosis
        modem.sendAT(GF("+CGDCONT?"));
        String pdp;
        modem.waitResponse(5000, pdp);
        Serial.print("[AT+CGDCONT?] ");
        Serial.println(pdp);
        Serial.flush();
        return;
    }

    // ---- Verify connection ------------------------------------------------
    String ip          = modem.getLocalIP();
    bool   isConnected = modem.isGprsConnected();

    Serial.print("[SIM] Local IP:         ");
    Serial.println(ip);
    Serial.print("[SIM] isGprsConnected(): ");
    Serial.println(isConnected ? "true" : "false");

    bool pass = isConnected
             && (ip.length() > 0)
             && (ip != "0.0.0.0")
             && (ip != "");

    Serial.println("----------------------------------------");
    if (pass) {
        Serial.println("[TEST 2][PASS] GPRS connected, valid IP received");
    } else {
        if (!isConnected) Serial.println("[TEST 2][FAIL] isGprsConnected() returned false");
        if (ip == "0.0.0.0" || ip.length() == 0)
            Serial.println("[TEST 2][FAIL] IP address is 0.0.0.0 or empty");
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
