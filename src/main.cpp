#include <Arduino.h>
#include <Wire.h>
#include "sensor_light.h"    // BH1750
#include "sensor_temp_hum.h" // SHT31
#include "sensor_co2.h"      // SCD40
#include "sensor_pressure.h" // BME280
#include "sensor_gas.h"      // MQ-4 (Analog)
#include "sensor_soil.h"     // Soil Moisture (Analog)
#include "fan.h"             // Fan/Relay control
#include "sim_modun.h"      // SIM module functions

#define RELAY_PIN PA1 
#define LED_PIN PC13 

unsigned long lastmessage = 0;
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
  setupSIM_A7680();
  setup_Actuators();
  setupBH1750_Sensor();
  setupSHT31_Sensor();
  setupSCD40_Sensor();
  setupBME280_Sensor();
  
  Serial.println(" HE THONG DA SAN SANG ");
}

void loop() {

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

  // --- Logic điều khiển  ---
  if (co2 > 1000 || t > 30.0 || gasValue > 500) {
    digitalWrite(RELAY_PIN, LOW); 
    digitalWrite(LED_PIN, LOW);  
    control_Fan(true);
    if (!alertSent){
      String message = "CO2: " + String(co2) + " ppm, Temp: " + String(t) + " C, Gas: " + String(gasValue) + "Soil: " + String(soilMoisture) + "%";
      sendSMS_Alert("+84xxxxxxxxx", message);
      alertSent = true;
    }
  } else {
    digitalWrite(RELAY_PIN, HIGH); 
    digitalWrite(LED_PIN, HIGH);
    control_Fan(false);
    alertSent = false;
  }
  updateSIM_Connection();

  delay(2000); 
}