
#include <Arduino.h>
#include <Wire.h>
#include "sensor_light.h"    // BH1750
#include "sensor_temp_hum.h" // SHT31
#include "sensor_co2.h"      // SCD40
#include "sensor_pressure.h" // BME280
#include "sensor_gas.h"      // MQ-4 (Analog)
#include "sensor_soil.h"     // Soil Moisture (Analog)
#include "gadget.h"             // Fan/Relay control
#include "sim_modun.h"      // SIM module functions

#define RELAY_PIN PA1 
#define LED_PIN PC13 
// sim pre setup
const String phoneNumber = "+84xxxxxxxxx"; // Số điện thoại nhận cảnh báo
unsigned long last_period_message = 0;
const unsigned long interval_message = 3600000;
bool alertSent = false;

void setup() {

  Serial.begin(115200);
  delay(2000);


  Wire.setSDA(PB7);
  Wire.setSCL(PB6);
  Wire.begin();

  // Cấu hình chân Relay và LED
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Tắt Relay ban đầu
  digitalWrite(LED_PIN, HIGH);   

  Serial.println(" DANG KHOI TAO HE THONG CAM BIEN ");
  //setupSIM_A7680();
  //setup_Actuators();
  setupBH1750_Sensor();
  setupSHT30_Sensor();
  setupSCD40_Sensor();
  setupBME280_Sensor();
  
  Serial.println(" HE THONG DA SAN SANG ");
}

void loop() {
  
  //check_PhysicalButtons();
  float lux = readBH1750_Lux();
  float t, h;
  readSHT30_Data(t, h);
  uint16_t co2 = readSCD40_CO2();
  float pressure = readBME280_Pressure();
  int gasValue = readMQ4_Gas();
  //int soilMoisture = readSoil_Moisture();
  unsigned long currentMillis = millis(); 

  // --- In dữ liệu ra Serial Monitor ---
  Serial.println("------------------------------------");
  Serial.printf("CO2: %u ppm | Temp: %.2f *C | Hum: %.2f %%\n", co2, t, h);
  Serial.printf("Light: %.1f Lux | Pressure: %.1f hPa\n", lux, pressure);
  Serial.printf("Gas MQ4: %d ", gasValue);
  //Serial.printf("Gas MQ4: %d | Soil: %d %%\n", gasValue, soilMoisture);

  // --- Logic điều khiển  ---
  /*
  if (co2 > 1000 || t > 30.0 || gasValue > 500) {
    digitalWrite(RELAY_PIN, LOW); 
    digitalWrite(LED_PIN, LOW);  
    control_Fan(true);
    /*if (currentMillis - last_period_message >= interval_message) {
      String message = "information at n times:\n";
      message += "CO2: " + String(co2) + " ppm\n"; //SCD40
      message += "Temp: " + String(t) + " C\n"; //SHT30
      message += "Hum: " + String(h) + " %\n"; //SHT30
      message += "Gas: " + String(gasValue) + "\n";//MQ4
      message += "Soil: " + String(soilMoisture) + "%\n";//Soil
      message += "Light: " + String(lux) + " Lux\n"; //BH1750
      message += "Pressure: " + String(pressure) + " hPa"; //BME280
      //SCD40 - SHT30 - MQ4 - Soil - BH1750 - BME280
      alertSent = true;
      sendSMS_Alert(phoneNumber, message);
      last_period_message = currentMillis;
     
    }
      */

  /*} else {
    digitalWrite(RELAY_PIN, HIGH); 
    digitalWrite(LED_PIN, HIGH);
    control_Fan(false);
    //alertSent = false;
  }
  */
  //updateSIM_Connection();

  delay(2000); 
}