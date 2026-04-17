// ============================================================
//  TEST 1 — SIM Power & AT Response
//
//  Goal: Confirm A7680C is alive and responds to raw AT commands
//        on USART2 (PA3=RX, PA2=TX) at 115200 baud.
//
//  Hardware: STM32-B (Blue Pill), SIM A7680C on 4V/2A supply.
//            STM32-A NOT required.
//
//  Pass criteria:
//    • AT → OK
//    • AT+CIMI → 15-digit IMSI  (SIM card detected)
//    • AT+CSQ  → RSSI > 5  (usable signal)
//
//  Output: RTT channel 0  (JLinkRTTViewer or openocd telnet :19021)
// ============================================================

#include <Arduino.h>
#include "SEGGER_RTT.h"

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

// ---- SIM UART: USART2  PA3(RX) / PA2(TX) --------------------------------
static HardwareSerial SerialSIM(PA3, PA2);

static bool testPassed = true;

// Send raw AT command, collect response until "OK" or "ERROR" or timeout
static String sendATRaw(const char* cmd, unsigned long waitMs = 2000) {
    // Drain any stale bytes
    while (SerialSIM.available()) SerialSIM.read();

    SerialSIM.print(cmd);
    SerialSIM.print("\r\n");

    unsigned long start = millis();
    String resp = "";
    while (millis() - start < waitMs) {
        while (SerialSIM.available()) resp += (char)SerialSIM.read();
        if (resp.indexOf("OK")    >= 0) break;
        if (resp.indexOf("ERROR") >= 0) break;
    }
    resp.trim();
    return resp;
}

// ---- setup ---------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  TEST 1 — SIM Power & AT Response");
    Serial.println("========================================");
    Serial.flush();

    pinMode(PC13, OUTPUT);
    digitalWrite(PC13, HIGH);   // LED off (active-low)

    SerialSIM.begin(115200);

    // A7680C needs ~8 s after power-on before it accepts AT
    Serial.println("[SIM] Cold-boot wait 8s...");
    Serial.flush();
    delay(8000);

    // ---- Probe: 3 retries ------------------------------------------------
    bool alive = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        Serial.print("[AT] Probe ");
        Serial.print(attempt);
        Serial.println("/3 — sending AT");
        String r = sendATRaw("AT", 2000);
        Serial.print("[AT] <<< ");
        Serial.println(r);
        if (r.indexOf("OK") >= 0) { alive = true; break; }
        delay(1000);
    }

    if (!alive) {
        Serial.println("[TEST 1][FAIL] No AT response after 3 retries");
        Serial.println("[HINT] Check PA2/PA3 wiring and 4V/2A SIM power supply");
        Serial.flush();
        return;
    }
    Serial.println("[AT] Modem alive.");

    // ---- AT+GMM — model string -------------------------------------------
    Serial.println("[AT] >>> AT+GMM");
    String gmm = sendATRaw("AT+GMM", 2000);
    Serial.print("[AT] <<< ");
    Serial.println(gmm);
    // Any non-empty response counts — just verifying comms work

    // ---- AT+CIMI — IMSI (15-digit decimal) --------------------------------
    Serial.println("[AT] >>> AT+CIMI");
    String imsiResp = sendATRaw("AT+CIMI", 3000);
    Serial.print("[AT] <<< ");
    Serial.println(imsiResp);

    bool imsiOk = false;
    // Search for a run of 15 consecutive digits anywhere in the response
    for (int i = 0; i <= (int)imsiResp.length() - 15; i++) {
        bool all = true;
        for (int j = 0; j < 15; j++) {
            if (!isdigit((unsigned char)imsiResp[i + j])) { all = false; break; }
        }
        if (all) { imsiOk = true; break; }
    }
    if (imsiOk) {
        Serial.println("[TEST 1] CIMI OK — SIM card detected");
    } else {
        Serial.println("[TEST 1][FAIL] AT+CIMI did not return 15-digit IMSI");
        Serial.println("[HINT] Is a SIM card inserted? Is it Vietnamobile m-wap SIM?");
        testPassed = false;
    }

    // ---- AT+CSQ — signal quality -----------------------------------------
    Serial.println("[AT] >>> AT+CSQ");
    String csqResp = sendATRaw("AT+CSQ", 2000);
    Serial.print("[AT] <<< ");
    Serial.println(csqResp);

    int csqIdx = csqResp.indexOf("+CSQ:");
    if (csqIdx >= 0) {
        String csqPart = csqResp.substring(csqIdx + 5);
        csqPart.trim();
        int comma = csqPart.indexOf(',');
        if (comma > 0) csqPart = csqPart.substring(0, comma);
        int rssi = csqPart.toInt();
        Serial.print("[AT] RSSI=");
        Serial.print(rssi);
        if (rssi == 99) {
            Serial.println(" (unknown — antenna disconnected?)");
            testPassed = false;
        } else if (rssi < 5) {
            Serial.println(" (too weak — move to better coverage area)");
            testPassed = false;
        } else {
            Serial.print(" — approx ");
            Serial.print(-113 + rssi * 2);
            Serial.println(" dBm  OK");
        }
    } else {
        Serial.println("[TEST 1][FAIL] AT+CSQ response not parseable");
        testPassed = false;
    }

    // ---- Summary ----------------------------------------------------------
    Serial.println("----------------------------------------");
    if (testPassed) {
        Serial.println("[TEST 1][PASS] Modem alive, SIM detected, signal OK");
    } else {
        Serial.println("[TEST 1][FAIL] One or more checks failed — see above");
    }
    Serial.println("========================================");
    Serial.flush();
}

void loop() {
    // Blink PC13 every 500 ms to confirm loop() is running
    static unsigned long lastBlink = 0;
    if (millis() - lastBlink > 500) {
        lastBlink = millis();
        digitalWrite(PC13, !digitalRead(PC13));
    }
}
