#include <Wire.h>
#include <BH1750.h>   // Thư viện hệ thống (phải để trong ngoặc < >)
#include "bh1750.h"   // File cá nhân của bạn (để trong ngoặc " ")

BH1750 lightMeter;

void setupBH1750_Sensor() {
    // Với STM32, nên kiểm tra xem Wire đã begin ở main chưa
    if(!lightMeter.begin()) {
        Serial.println("BH1750 Error!");
    }
}

float readBH1750_Lux() {
    return lightMeter.readLightLevel();
}