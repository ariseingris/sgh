#include "sensor_soil.h"
int readSoil_Moisture() { 
    int val = analogRead(PA1);
    return map(val, 4095, 0, 0, 100); 
}