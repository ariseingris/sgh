#include <Arduino.h>
#include "sensor_pressure.h"
#include <Adafruit_BME280.h>
#include <Wire.h>
#include "rtt_debug.h"

// ============================================================
//  sensor_pressure.cpp  —  BME280 barometric pressure  (STM32-A)
//
//  BUG FIX: Removed undefined Serial references.
//    This file now properly includes rtt_debug.h and uses
//    extern RTTSerial rttDebug for all debug output.
//
//  BUG FIX: Added bme280Ready flag to track initialization status.
//    begin() return value is now checked. If sensor fails to init,
//    readBME280_Pressure() returns 0.0 with an error log.
//
//  BUG FIX: Added range validation (300-1100 hPa typical).
//    Out-of-range readings are logged and ignored to prevent
//    corrupted data transmission to STM32B.
// ============================================================

static Adafruit_BME280 bme;
static bool bme280Ready = false;

void setupBME280_Sensor() {
    if (!bme.begin(0x76)) {
        extern RTTSerial rttDebug;
        rttDebug.println("[BME280] Init FAILED at 0x76 — check I2C wiring.");
        bme280Ready = false;
        return;
    }
    bme280Ready = true;
}

float readBME280_Pressure() {
    if (!bme280Ready) {
        return 0.0f;
    }

    float pressure = bme.readPressure() / 100.0f;  // Convert Pa to hPa

    // Validate range: typical atmospheric 300–1100 hPa (extreme: ~100–1200 hPa)
    if (pressure < 100.0f || pressure > 1200.0f) {
        extern RTTSerial rttDebug;
        rttDebug.print("[BME280] Out-of-range pressure: ");
        rttDebug.print(pressure);
        rttDebug.println(" hPa");
        return 0.0f;
    }

    return pressure;
}

bool isBME280_Ready() {
    return bme280Ready;
}