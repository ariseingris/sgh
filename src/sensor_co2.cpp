#include "sensor_co2.h" // Phải khớp với tên file .h mới
#include <SensirionI2CScd4x.h>

SensirionI2CScd4x scd4x;

void setupSCD40_Sensor() {
    scd4x.begin(Wire);
    scd4x.startPeriodicMeasurement();
}

uint16_t readSCD40_CO2() {
    uint16_t co2 = 0;
    float t, h;
    scd4x.readMeasurement(co2, t, h);
    return co2;
}