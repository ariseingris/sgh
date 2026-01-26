#include <Arduino.h>
#include <Wire.h>

// Include các file header đã đổi tên
#include "sensor_light.h"    // BH1750
#include "sensor_temp_hum.h" // SHT30/31
#include "sensor_co2.h"      // SCD40
#include "sensor_pressure.h" // BME280
#include "sensor_gas.h"      // MQ-4 (Analog)
#include "sensor_soil.h"     // Soil Moisture (Analog)

// Định nghĩa chân cho Relay và LED
#define RELAY_PIN PA1 
#define LED_PIN PC13 

void setup() {
  // 1. Khởi tạo cổng Serial (Dùng qua USB CDC theo file .ini của bạn)
  Serial.begin(115200);
  delay(2000); // Chờ Serial khởi động

  // 2. Cấu hình chân I2C cho STM32 Blue Pill
  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();

  // 3. Khởi tạo các chân IO
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Tắt relay (kích thấp)
  digitalWrite(LED_PIN, HIGH);   // Tắt LED

  Serial.println("--- DANG KHOI TAO HE THONG CAM BIEN ---");

  // 4. Khởi tạo từng cảm biến thông qua các hàm đã định nghĩa trong file .h
  setupBH1750_Sensor();
  setupSHT31_Sensor();
  setupSCD40_Sensor();
  setupBME280_Sensor();
  
  Serial.println("--- HE THONG DA SAN SANG ---");
}

void loop() {
  // --- Đọc dữ liệu từ các cảm biến ---
  float lux = readBH1750_Lux();
  float t, h;
  readSHT31_Data(t, h);
  uint16_t co2 = readSCD40_CO2();
  float pressure = readBME280_Pressure();
  int gasValue = readMQ4_Gas();
  int soilMoisture = readSoil_Moisture();

  // --- In dữ liệu ra Serial Monitor ---
  Serial.println("------------------------------------");
  Serial.printf("CO2: %u ppm | Temp: %.2f *C | Hum: %.2f %%\n", co2, t, h);
  Serial.printf("Light: %.1f Lux | Pressure: %.1f hPa\n", lux, pressure);
  Serial.printf("Gas MQ4: %d | Soil: %d %%\n", gasValue, soilMoisture);

  // --- Logic điều khiển mẫu ---
  // Ví dụ: Nếu CO2 > 1000 hoặc Temp > 30 thì bật Relay (quạt/thông gió)
  if (co2 > 1000 || t > 30.0) {
    digitalWrite(RELAY_PIN, LOW); // BẬT
    digitalWrite(LED_PIN, LOW);   // Đèn báo hiệu
  } else {
    digitalWrite(RELAY_PIN, HIGH); // TẮT
    digitalWrite(LED_PIN, HIGH);
  }

  delay(2000); // Đợi 2 giây trước khi lặp lại
}