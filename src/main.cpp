
#include <Arduino.h>
#include <Wire.h>
#include "sensor_light.h"    // BH1750
#include "sensor_temp_hum.h" // SHT31
#include "sensor_co2.h"      // SCD40
#include "sensor_pressure.h" // BME280
#include "sensor_gas.h"      // MQ-4 (Analog)
#include "sensor_soil.h"     // Soil Moisture (Analog)
#include "gadget.h"          // Fan/Relay control
#include "sim_modun.h"      // SIM module functions

#define RELAY_PIN PA1 
#define LED_PIN PC13 
// sim pre setup
const String phoneNumber = "+84xxxxxxxxx"; // alert phone number
unsigned long last_period_message = 0;
const unsigned long interval_message = 3600000;
bool alertSent = false;

void setup() {

  Serial.begin(115200);
  delay(2000);

// i2c setup
  Wire.begin();

  // Cấu hình chân Relay và LED
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Tắt Relay ban đầu
  digitalWrite(LED_PIN, HIGH);   

  Serial.println(" DANG KHOI TAO HE THONG CAM BIEN ");
  setupSIM_A7680();
  setup_Actuators();
  setupBH1750_Sensor();
  setupSHT30_Sensor();
  setupSCD40_Sensor();
  setupBME280_Sensor();
  
  Serial.println(" HE THONG DA SAN SANG ");
}

void loop() {
  
  check_PhysicalButtons();
  float lux = readBH1750_Lux();
  float t = 0.0, h = 0.0;
  readSHT30_Data(t, h);
  float pressure = readBME280_Pressure();
  int gasValue = readMQ4_Gas();
  int soilMoisture = readSoil_Moisture();
  unsigned long currentMillis = millis(); 
  bool dataReady = isSCD40_DataReady();
  
  // --- In dữ liệu ra Serial Monitor ---
  if(dataReady) {
    uint16_t co2 = readSCD40_CO2();
    Serial.println("----- Du lieu cam bien -----");
    Serial.print("Anh sang: ");
    Serial.print(lux);
    Serial.println(" Lux");

    Serial.print("Nhiet do SHT30: ");
    Serial.print(t);
    Serial.println(" C");

    Serial.print("Do am SHT30: ");
    Serial.print(h);
    Serial.println(" %");

    Serial.print("CO2 SCD40: ");
    Serial.print(co2);
    Serial.println(" ppm");

    Serial.print("Ap suat BME280: ");
    Serial.print(pressure);
    Serial.println(" hPa");

    delay(5000); // Wait 5 seconds before next read
  } else {
    Serial.println("Du lieu CO2 chua san sang, bo qua lan doc nay.");
    delay(1000); // Wait 1 second before retry
  }

  // --- Logic control  ---
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