#ifndef SENSOR_GAS_H
#define SENSOR_GAS_H
#include <Arduino.h>

void initMQ4_Warmup();
bool isMQ4_Warm();
int readMQ4_Gas();
#endif