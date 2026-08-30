#ifndef SENSOR_TEMP_HUM_H
#define SENSOR_TEMP_HUM_H

#include <Arduino.h>

void setupSHT30_Sensor();
void readSHT30_Data(float &t, float &h);
bool isSHT30_Ready();

#endif