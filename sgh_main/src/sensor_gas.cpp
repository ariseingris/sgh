// ============================================================
//  sensor_gas.cpp  —  MQ-4 methane/gas sensor  (STM32-A)
//
//  BUG FIX: Added documentation for pin and range validation.
//    MQ-4 outputs analog voltage (0–3.3V on STM32) → ADC (0–4095).
//    Valid range: 0–4095. Out-of-range values are clamped to
//    prevent data transmission errors to STM32-B.
//
//  NOTE: MQ-4 requires 24 hours warm-up time after power-on
//    for accurate calibration. During this period, readings may
//    be unstable. Consider implementing discarding of early samples.
// ============================================================

#include "sensor_gas.h"

// ============================================================
//  readMQ4_Gas()
//  Reads analog gas sensor (MQ-4) on pin PB1
//  Returns: 0–4095 ADC value (0V = 0; 3.3V = 4095)
// ============================================================
int readMQ4_Gas() {
    int val = analogRead(PB1);

    // Validate ADC range (safety check for hardware issues)
    if (val < 0 || val > 4095) {
        return 0;  // Clamp to zero on out-of-range error
    }

    return val;
}