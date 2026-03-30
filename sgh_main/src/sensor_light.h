#ifndef BH1750_CUSTOM_H
#define BH1750_CUSTOM_H

#include <Arduino.h>

void setupBH1750_Sensor();
float readBH1750_Lux();
bool isBH1750_Ready();

#endif