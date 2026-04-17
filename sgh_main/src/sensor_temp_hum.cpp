#include "sensor_temp_hum.h"
#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <math.h>
#include "rtt_debug.h"

// ============================================================
//  sensor_temp_hum.cpp  —  SHT30 temperature/humidity  (STM32-A)
//
//  BUG FIX: Removed undefined Serial references.
//    This file now properly includes rtt_debug.h and uses
//    extern RTTSerial rttDebug for all debug output.
//
//  BUG FIX: Added sht30Ready flag to track initialization status.
//    begin() return values are now checked and stored.
//    readSHT30_Data() returns error status via out parameters.
// ============================================================

static Adafruit_SHT31 sht30 = Adafruit_SHT31();
static bool sht30Ready = false;

void setupSHT30_Sensor() {
    if (!sht30.begin(0x44)) {
        extern RTTSerial rttDebug;
        rttDebug.println("[SHT30] FAILED at 0x44, trying 0x45...");
        if (!sht30.begin(0x45)) {
            rttDebug.println("[SHT30] FAILED at both addresses — check I2C wiring.");
            sht30Ready = false;
            return;
        }
    }
    sht30Ready = true;
}

void readSHT30_Data(float &temperature, float &humidity) {
    // PHASE 2 FIX: -999.0f sentinel instead of 0.0f.
    // 0.0°C / 0.0% looks like a real reading (or a greenhouse freeze alarm).
    // -999.0f is clearly outside all physical ranges — cloud can display "N/A".
    temperature = -999.0f;
    humidity    = -999.0f;

    if (!sht30Ready) {
        return;
    }

    float t = sht30.readTemperature();
    float h = sht30.readHumidity();

    // Check for NaN or out-of-range values (SHT30: -40 to 125°C, 0-100% RH)
    if (isnan(t) || isnan(h) || t < -40.0f || t > 125.0f || h < 0.0f || h > 100.0f) {
        extern RTTSerial rttDebug;
        rttDebug.println("[SHT30] Read failed or out of range!");
        return;
    }

    temperature = t;
    humidity = h;
}

bool isSHT30_Ready() {
    return sht30Ready;
}
