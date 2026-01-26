#ifndef SENSOR_TEMP_HUM_H
#define SENSOR_TEMP_HUM_H
#include <Arduino.h>

void setupSHT31_Sensor();
void readSHT31_Data(float &t, float &h);

#endif