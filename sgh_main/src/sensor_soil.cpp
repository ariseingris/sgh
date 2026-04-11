// ============================================================
//  sensor_soil.cpp  —  Soil moisture sensor  (STM32-A)
//
//  BUG FIX: Added documentation for pin, ranging, and validation.
//    Pin PA0 reads analog soil moisture (0–3.3V) → ADC (0–4095).
//    Maps raw ADC to percentage: 4095 (dry) → 0%, 0 (wet) → 100%.
//    Out-of-range ADC values are clamped before mapping.
//
//  NOTE: Capacitive soil sensors are preferred over resistive
//    for longevity in wet soil environments. Consider firmware
//    calibration routine if accuracy drifts over time.
// ============================================================

#include "sensor_soil.h"
#include "rtt_debug.h"

// ============================================================
//  readSoil_Moisture()
//  Reads capacitive soil moisture sensor on pin PA0
//  Maps ADC value to percentage (0–100%)
//  Returns: 0–100 percent
// ============================================================
int readSoil_Moisture() {
    int val = analogRead(PA0);

    // Clamp ADC value to valid range (0–4095)
    if (val < 0) val = 0;
    if (val > 4095) val = 4095;

    // Map: 4095 (soil dry/open) = 0%, 0 (soil saturated) = 100%
    return map(val, 4095, 0, 0, 100);
}