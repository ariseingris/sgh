// ============================================================
//  TEST 5 — Float JSON Payload Format  (pure firmware test)
//
//  Goal: Verify that -Wl,-u_printf_float in platformio.ini causes
//        snprintf() to format floating-point values correctly on
//        newlib-nano (the default libc for STM32/Arduino).
//
//  Without the linker flag, newlib-nano strips float support from
//  snprintf to save ~8 KB of flash. All %f / %.1f fields produce
//  an empty string instead of a number.
//
//  No hardware required — this is a compile-and-run logic test.
//
//  Pass criteria:
//    snprintf produces exactly:
//    {"temp":28.5,"hum":72.3,"co2":625,"lux":480,"pressure":1013.2}
//
//  Output: RTT channel 0
// ============================================================

#include <Arduino.h>
#include <string.h>
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

// The exact string we expect from a correctly-linked snprintf
static const char EXPECTED[] =
    "{\"temp\":28.5,\"hum\":72.3,\"co2\":625,\"lux\":480,\"pressure\":1013.2}";

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n========================================");
    Serial.println("  TEST 5 — Float JSON Payload Format");
    Serial.println("  (no hardware needed)");
    Serial.println("========================================");
    Serial.flush();

    // ---- snprintf test --------------------------------------------------
    float temp     = 28.5f;
    float hum      = 72.3f;
    float pressure = 1013.2f;
    int   co2      = 625;
    int   lux      = 480;

    char buf[128];
    int written = snprintf(buf, sizeof(buf),
        "{\"temp\":%.1f,\"hum\":%.1f,\"co2\":%d,\"lux\":%d,\"pressure\":%.1f}",
        temp, hum, co2, lux, pressure);

    Serial.print("[TEST 5] Expected: ");
    Serial.println(EXPECTED);
    Serial.print("[TEST 5] Got:      ");
    Serial.println(buf);
    Serial.print("[TEST 5] snprintf returned: ");
    Serial.println(written);

    bool pass = true;

    if (written <= 0) {
        Serial.println("[TEST 5][FAIL] snprintf returned 0 or negative");
        pass = false;
    }

    if (strcmp(buf, EXPECTED) != 0) {
        Serial.println("[TEST 5][FAIL] Output mismatch — diagnosing fields:");

        if (strstr(buf, "28.5") == nullptr) {
            Serial.println("  • temp:     WRONG/EMPTY — float not formatted");
        }
        if (strstr(buf, "72.3") == nullptr) {
            Serial.println("  • hum:      WRONG/EMPTY — float not formatted");
        }
        if (strstr(buf, "1013.2") == nullptr) {
            Serial.println("  • pressure: WRONG/EMPTY — float not formatted");
        }
        if (strstr(buf, "625") == nullptr) {
            Serial.println("  • co2:      WRONG (int field, should always work)");
        }
        if (strstr(buf, "480") == nullptr) {
            Serial.println("  • lux:      WRONG (int field, should always work)");
        }

        Serial.println("[HINT] Missing -Wl,-u_printf_float in platformio.ini build_flags");
        Serial.println("[HINT] Ensure no space: -Wl,-u_printf_float  (not -Wl, -u_printf_float)");
        pass = false;
    }

    // ---- Bonus: Arduino String(float, decimals) --------------------------
    // module_sim.cpp uses String(float, 1) as an alternative.
    // Verify it also works correctly.
    String tempStr     = String(temp,     1);
    String humStr      = String(hum,      1);
    String pressureStr = String(pressure, 1);

    Serial.print("[TEST 5] String(28.5f,  1) = '");
    Serial.print(tempStr);
    Serial.println("'");
    Serial.print("[TEST 5] String(72.3f,  1) = '");
    Serial.print(humStr);
    Serial.println("'");
    Serial.print("[TEST 5] String(1013.2f,1) = '");
    Serial.print(pressureStr);
    Serial.println("'");

    if (tempStr != "28.5" || humStr != "72.3" || pressureStr != "1013.2") {
        Serial.println("[TEST 5][FAIL] Arduino String(float,1) also incorrect");
        pass = false;
    }

    // ---- Summary ---------------------------------------------------------
    Serial.println("----------------------------------------");
    if (pass) {
        Serial.println("[TEST 5][PASS] snprintf and String() float formatting correct");
    } else {
        Serial.println("[TEST 5][FAIL] See field diagnosis above");
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
