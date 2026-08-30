#ifndef RTT_DEBUG_H
#define RTT_DEBUG_H

// ============================================================
//  rtt_debug.h  —  SEGGER RTT wrapper  (STM32-A / sgh_main)
//
//  FW-3 FIX: Now extends Stream (not Print) to match STM32-B.
//  Any library that calls Serial.available() / Serial.read()
//  via the Stream interface will work correctly.
//
//  RULES — must not be violated:
//  1. DO NOT put  #define Serial rttDebug  here.
//  2. available() and read() MUST call real RTT functions.
// ============================================================

#include "SEGGER_RTT.h"
#include <Arduino.h>

class RTTSerial : public Stream {
public:
    void begin(unsigned long /*baud*/) { SEGGER_RTT_Init(); }

    // ----- output (Print) -----
    size_t write(uint8_t c) override {
        SEGGER_RTT_Write(0, &c, 1);
        return 1;
    }
    size_t write(const uint8_t* b, size_t s) override {
        SEGGER_RTT_Write(0, b, s);
        return s;
    }

    // Spin until RTT up-buffer drained (max 200ms).
    void flush() override {
        uint32_t t0 = millis();
        while (SEGGER_RTT_HasDataUp(0)) {
            if (millis() - t0 > 200) break;
        }
    }

    // ----- input (Stream) — RTT channel 0 down-buffer -----
    int available() override {
        return (int)SEGGER_RTT_HasData(0);
    }

    int read() override {
        unsigned char c = 0;
        return (SEGGER_RTT_Read(0, &c, 1) == 1) ? (int)c : -1;
    }

    int peek() override { return -1; }  // RTT has no peek
};

extern RTTSerial rttDebug;  // single definition in main.cpp

// NOTE: #define Serial rttDebug belongs only in main.cpp.

#endif // RTT_DEBUG_H