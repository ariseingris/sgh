#ifndef RTT_DEBUG_H
#define RTT_DEBUG_H

// ============================================================
//  rtt_debug.h  —  SEGGER RTT wrapper  (STM32-A / sgh_main)
//
//  RULES — must not be violated:
//
//  1. DO NOT put  #define Serial rttDebug  here.
//     It must only appear in main.cpp, after all #includes.
//     Putting it here leaks the macro into every .cpp that
//     includes this header (directly or via gadget.h), so
//     sensor files whose Serial.print() calls resolve to
//     the real uninitialized HardwareSerial get silently
//     redirected — or worse, sensor files that DON'T include
//     this header get the real Serial while gadget.cpp gets
//     rttDebug, making debug output inconsistent.
//
//  2. available() and read() MUST call the real RTT functions.
//     Hardcoding 0 / -1 makes Serial.available() always false,
//     which permanently kills the RTT terminal command handler
//     in main.cpp loop() (keys 1–6 never fire).
// ============================================================

#include "SEGGER_RTT.h"
#include <Arduino.h>

class RTTSerial : public Print {
public:
    void begin(unsigned long /*baud*/) { SEGGER_RTT_Init(); }

    size_t write(uint8_t c) override {
        SEGGER_RTT_Write(0, &c, 1);
        return 1;
    }
    size_t write(const uint8_t* b, size_t s) override {
        SEGGER_RTT_Write(0, b, s);
        return s;
    }

    // A-BUG-2 FIX: was hardcoded `return 0`
    // → Serial.available() in loop() was always 0, RTT keys dead.
    int available() {
        return (int)SEGGER_RTT_HasData(0);
    }

    // A-BUG-3 FIX: was hardcoded `return -1`
    // → Serial.read() never returned actual input even if available() worked.
    int read() {
        unsigned char c = 0;
        return (SEGGER_RTT_Read(0, &c, 1) == 1) ? (int)c : -1;
    }
};

extern RTTSerial rttDebug;  // single definition in main.cpp

// A-BUG-1 FIX: #define Serial rttDebug REMOVED from this header.
// It belongs only in main.cpp, after all library #includes.

#endif // RTT_DEBUG_H