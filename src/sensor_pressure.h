#ifndef SENSOR_PRESSURE_H
#define SENSOR_PRESSURE_H
#include <Arduino.h>

void setupBME280_Sensor();
float readBME280_Pressure();
#endif