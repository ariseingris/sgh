#include <Arduino.h>
#include <Wire.h>
#include "sensor_co2.h" 
#include <SensirionI2CScd4x.h>

SensirionI2CScd4x scd4x;
#define SCD4X_I2C_ADDRESS 0x62

void setupSCD40_Sensor() {
    scd4x.begin(Wire);
    scd4x.stopPeriodicMeasurement();
    scd4x.startPeriodicMeasurement();

}

uint16_t readSCD40_CO2() {
    uint16_t co2 = 0;
    float t, h;
    uint16_t error = scd4x.readMeasurement(co2, t, h);
    if (error) {
        Serial.print("[SCD40] readMeasurement error: ");
        Serial.println(error);
        return 0;
    }
    if (co2 == 0) {
        Serial.println("[SCD40] CO2=0 — measurement not valid yet");
        return 0;
    }
    return co2;
}
bool isSCD40_DataReady() {
    bool dataReady = false;
    scd4x.getDataReadyFlag(dataReady);
    return dataReady;
}