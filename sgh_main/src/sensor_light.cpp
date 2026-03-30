// ============================================================
//  sensor_light.cpp  —  BH1750 ambient light  (STM32-A)
//
//  A-BUG-4 FIX: begin() now passes CONTINUOUS_HIGH_RES_MODE.
//    Default mode (no argument) is ONE_TIME_H_RES_MODE: sensor
//    takes one measurement then powers itself down. Every call to
//    readLightLevel() after the first returns -1.0 because the
//    sensor is asleep. This made lux always read as -1 / 0.
//
//  A-BUG-5 FIX: readBH1750_Lux() now clamps -1.0 error return
//    to 0.0 so upstream code and the DATA: payload to STM32-B
//    never contain a negative lux value.
//
//  BUG FIX: Added range validation (0–65000 lux typical).
//    Out-of-range readings are logged and clamped to 0.
//
//  NOTE: Serial is NOT available in this file.
//    rtt_debug.h is not included here (sensor_light.h doesn't
//    pull it in). The #define Serial rttDebug lives only in
//    main.cpp. If a debug print is needed, use rttDebug directly:
//      extern RTTSerial rttDebug;
//      rttDebug.println("...");
// ============================================================

#include <Wire.h>
#include <BH1750.h>
#include "sensor_light.h"
#include "rtt_debug.h"

static BH1750 lightMeter;
static bool bh1750Ready = false;

void setupBH1750_Sensor() {
    // CONTINUOUS_HIGH_RES_MODE: 1 lx resolution, ~120 ms/sample,
    // sensor stays active — readLightLevel() always returns fresh data.
    if (!lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE)) {
        extern RTTSerial rttDebug;
        rttDebug.println("[BH1750] begin() FAILED — check I2C wiring to 0x23");
        bh1750Ready = false;
        return;
    }
    bh1750Ready = true;
}

float readBH1750_Lux() {
    if (!bh1750Ready) {
        return 0.0f;
    }

    float lux = lightMeter.readLightLevel();
    
    // readLightLevel() returns -1.0 on sensor error or when the
    // sensor is in ONE_TIME mode and has already taken its reading.
    if (lux < 0.0f || lux > 65000.0f) {
        extern RTTSerial rttDebug;
        rttDebug.print("[BH1750] Out of range: ");
        rttDebug.println(lux);
        return 0.0f;
    }

    return lux;
}

bool isBH1750_Ready() {
    return bh1750Ready;
}