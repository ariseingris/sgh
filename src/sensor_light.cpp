#include <Wire.h>
#include <BH1750.h>   
#include "bh1750.h"   

BH1750 lightMeter;

void setupBH1750_Sensor() {
    if(!lightMeter.begin()) {
        Serial.println("BH1750 Error!");
    }
}
float readBH1750_Lux() {
    return lightMeter.readLightLevel();
}