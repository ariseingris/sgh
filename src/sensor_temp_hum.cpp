#include "sensor_temp_hum.h"
#include <Wire.h>
#include "Adafruit_SHT31.h"

// Khởi tạo đối tượng cho SHT30 (dùng chung thư viện SHT31)
Adafruit_SHT31 sht30 = Adafruit_SHT31();

void setupSHT30_Sensor() {
    // Địa chỉ I2C mặc định của SHT30 là 0x44
    // Đảm bảo dây SDA nối PB7 và SCL nối PB6
    if (!sht30.begin(0x44)) { 
        Serial.println("LOI: Khong tim thay cam bien SHT30!");
    } else {
        Serial.println("SHT30 da san sang.");
    }
}

void readSHT30_Data(float &t, float &h) {
    float temp = sht30.readTemperature();
    float hum = sht30.readHumidity();

    if (!isnan(temp) && !isnan(hum)) {
        t = temp;
        h = hum;
    } else {
        t = -999.0; // Giá trị báo lỗi
        h = -999.0;
        Serial.println("Loi: Khong the doc du lieu tu SHT30!");
    }
}