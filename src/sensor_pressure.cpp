#include "sensor_pressure.h"
#include <Adafruit_BME280.h>

Adafruit_BME280 bme;

void setupBME280_Sensor() {
    if(!bme.begin(0x76)) Serial.println("BME280 Error!");
}

float readBME280_Pressure() {
    return bme.readPressure() / 100.0F;
}