#include <Arduino.h>
#include <Wire.h>
#include "sensor_co2.h"
#include "rtt_debug.h"
#include <SensirionI2CScd4x.h>

// ============================================================
//  sensor_co2.cpp  —  SCD40 CO2 sensor  (STM32-A)
//
//  FIX (Serial conflict): All Serial.print() calls replaced with
//  rttDebug. The hardware Serial / USART1 peripheral is used
//  exclusively for STM32-A ↔ STM32-B communication. Using Serial
//  here would corrupt that UART link.
//
//  FIX (SCD40 error handling): Return values from all Sensirion
//  library calls are now checked:
//    - setupSCD40_Sensor()  checks stopPeriodicMeasurement() and
//      startPeriodicMeasurement(); logs errors, marks sensor not ready.
//    - isSCD40_DataReady()  checks getDataReadyFlag() return value;
//      returns false on error rather than acting on a garbage flag.
// ============================================================

static SensirionI2CScd4x scd4x;
static bool scd40Ready = false;
static int scd40FailCount = 0;

// config
uint16_t readSCD40_CO2();
void setupSCD40_Sensor();
bool isSCD40_DataReady();


void setupSCD40_Sensor() {
    scd4x.begin(Wire);

    // FIX: check stopPeriodicMeasurement — can fail if sensor is in
    // an undefined state after a power glitch or I2C bus recovery.
    uint16_t err = scd4x.stopPeriodicMeasurement();
    if (err) {
        rttDebug.print("[SCD40] stopPeriodicMeasurement error: ");
        rttDebug.println(err);
        // Non-fatal — sensor may not have been measuring; continue.
    }

    // FIX: check startPeriodicMeasurement — if this fails the sensor
    // will never produce data and isSCD40_DataReady() will always be false.
    err = scd4x.startPeriodicMeasurement();
    if (err) {
        rttDebug.print("[SCD40] startPeriodicMeasurement error: ");
        rttDebug.println(err);
        scd40Ready = false;
        return;
    }

    scd40Ready = true;
}

uint16_t readSCD40_CO2() {
    if (!scd40Ready) return 0;

    uint16_t co2 = 0;
    float t, h;
    uint16_t error = scd4x.readMeasurement(co2, t, h);
    if (error) {
        // FIX: was Serial.print — now rttDebug
        rttDebug.print("[SCD40] readMeasurement error: ");
        rttDebug.println(error);
        return 0;
    }
    if (co2 == 0) {
        // FIX: was Serial.println — now rttDebug
        rttDebug.println("[SCD40] CO2=0 — measurement not valid yet");
        return 0;
    }
    return co2;
}

bool isSCD40_DataReady() {
    if (!scd40Ready) return false;

    bool dataReady = false;
    unit_16_t err = scd4x.getDataReadyFlag(dataReady);
    if(err){
        scd40FailCount++;
        rttDebug.print("[SCD40] getDataReadyFlag error: ");
        rttDebug.println(err);
        if (scd40FailCount >= 5) {
            rttDebug.println("[SCD40] - re-init cause errors");
            scd40FailCount = 0;
            setupSCD40_Sensor();
        }
        return false;
    }
    // FIX: getDataReadyFlag() returns a non-zero error code on I2C failure.
    // Previously the return value was discarded, so a bus error left
    // dataReady=false (initialised) — which looks correct but hides the error.
    // Now we check it: on error return false and log so the watchdog can act.
    uint16_t err = scd4x.getDataReadyFlag(dataReady);
    if (err) {
        rttDebug.print("[SCD40] getDataReadyFlag error: ");
        rttDebug.println(err);
        return false;
    }
    return dataReady;
}