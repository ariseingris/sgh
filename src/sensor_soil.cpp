#include "sensor_soil.h"
int readSoil_Moisture() { 
    int val = 0;
    val = analogRead(PA0);
    return map(val, 4095, 0, 0, 100); 
}