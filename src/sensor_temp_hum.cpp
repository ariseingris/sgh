#include "sensor_temp_hum.h"
#include <Wire.h>
#include "Adafruit_SHT31.h"

Adafruit_SHT31 sht31 = Adafruit_SHT31();

void setupSHT31_Sensor() {
    // STM32 use 0x44 format for SHT30
    if (!sht31.begin(0x44)) { 
        Serial.println("LOI: Khong tim thay SHT31 Adafruit!");
    } else {
        Serial.println("SHT31 Adafruit da san sang.");
    }
}

void readSHT31_Data(float &t, float &h) {
    float temp = sht31.readTemperature();
    float hum = sht31.readHumidity();

    if (!isnan(temp) && !isnan(hum)) {
        t = temp;
        h = hum;
    } else {
        t = -999.0;
        h = -999.0;
        Serial.println("Loi doc du lieu tu SHT31!");
    }
}