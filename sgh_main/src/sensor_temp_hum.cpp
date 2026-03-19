#include "sensor_temp_hum.h"
#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <math.h>

// Khởi tạo đối tượng cho SHT30 (dùng chung thư viện SHT31)
Adafruit_SHT31 sht30 = Adafruit_SHT31();

void setupSHT30_Sensor() {
    if(sht30.begin(0x44)) {   // Địa chỉ I2C mặc định của SHT30 là 0x44
    } else {
        Serial.println("Loi: Khong the tim thay SHT30!");
    }
}
void readSHT30_Data(float &temperature, float &humidity) {
    float t = sht30.readTemperature();
    float h = sht30.readHumidity();
    if (isnan(t) || isnan(h)) {
        Serial.println("SHT30 read failed!");
        temperature = NAN;
        humidity = NAN;
        return;
    }
    temperature = t;
    humidity = h;
}
