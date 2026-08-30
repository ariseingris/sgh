// ============================================================
//  main_rtt_test.cpp  —  MINIMAL RTT TEST for STM32-A
//
//  NO sensors, NO Wire, NO I2C — just RTT output + LED blink.
//  Purpose: confirm RTT pipeline works before adding sensors.
//  If this works, the crash is in sensor/I2C init.
//  If this also stuns, the issue is RTT setup or ST-Link.
// ============================================================

#include <Arduino.h>
#include "SEGGER_RTT.h"

void setup() {
    SEGGER_RTT_Init();
    SEGGER_RTT_WriteString(0, "====== STM32-A RTT TEST BOOT ======\r\n");

    pinMode(PC13, OUTPUT);
    digitalWrite(PC13, HIGH);

    SEGGER_RTT_WriteString(0, "[INIT] LED pin ready\r\n");
    SEGGER_RTT_WriteString(0, "[INIT] No sensors loaded — pure RTT test\r\n");
    SEGGER_RTT_WriteString(0, "[INIT] Setup complete. Entering loop...\r\n");
}

void loop() {
    static unsigned long last = 0;
    static unsigned long count = 0;

    if (millis() - last >= 1000) {
        last = millis();
        count++;

        // Blink LED+
        digitalWrite(PC13, !digitalRead(PC13));

        // Print to RTT
        char buf[48];
        snprintf(buf, sizeof(buf), "[LOOP] tick=%lu uptime=%lus\r\n",
                 count, millis() / 1000UL);
        SEGGER_RTT_WriteString(0, buf);
    }
}