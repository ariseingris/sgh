#ifndef SENSOR_CO2_H
#define SENSOR_CO2_H
#include <Arduino.h>

void setupSCD40_Sensor();
uint16_t readSCD40_CO2();
bool isSCD40_DataReady();
#endif