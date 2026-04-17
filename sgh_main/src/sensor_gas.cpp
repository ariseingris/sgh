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
#include "rtt_debug.h"

static bool mq4WarmupDone = false;
static unsigned long mq4StartMs = 0;

// MQ-4 requires ~24 hours warm-up for accurate calibration.
// After power-on we mark a start time; readings before warm-up
// complete are flagged by returning -1 (invalid sentinel).
void initMQ4_Warmup() {
    mq4StartMs = millis();
    mq4WarmupDone = false;
}

bool isMQ4_Warm() {
    if (mq4WarmupDone) return true;
    // 300 seconds (5 min) minimum for the sensor to stabilise enough
    // to give directionally useful readings, even if not fully calibrated.
    // Full 24h accuracy requires mq4StartMs + 86400000UL.
    if (millis() - mq4StartMs >= 300000UL) {
        mq4WarmupDone = true;
    }
    return mq4WarmupDone;
}

// ============================================================
//  readMQ4_Gas()
//  Reads analog gas sensor (MQ-4) on pin PB1
//  Returns: -1 during warm-up; 0–4095 ADC value after warm-up
// ============================================================
int readMQ4_Gas() {
    if (!isMQ4_Warm()) {
        return -1; // Sentinel: sensor warming up, value not valid
    }
    int val = analogRead(PB1);

    // Validate ADC range (safety check for hardware issues)
    if (val < 0 || val > 4095) {
        return 0;  // Clamp to zero on out-of-range error
    }

    return val;
}