
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

// sim pre setup
const String phoneNumber = "0814686688"; // alert phone number
unsigned long last_period_message = 0;
const unsigned long interval_message = 3600000;
bool alertSent = false;
bool systemActive = false; // Global system state

// Global sensor values - read once per loop
float g_lux = 0.0;
float g_temperature = 0.0;
float g_humidity = 0.0;
float g_pressure = 0.0;
int g_gasValue = 0;
int g_soilMoisture = 0;
uint16_t g_co2 = 0;
bool g_dataReady = false;

void activate_System() {
  systemActive = true;
  last_period_message = millis();
  close_System();
  
  if(g_dataReady) {
    Serial.println("----- Du lieu cam bien -----");
    Serial.print("Anh sang: ");
    Serial.print(g_lux);
    Serial.println(" Lux");

    Serial.print("Nhiet do SHT30: ");
    Serial.print(g_temperature);
    Serial.println(" C");

    Serial.print("Do am SHT30: ");
    Serial.print(g_humidity);
    Serial.println(" %");

    Serial.print("CO2 SCD40: ");
    Serial.print(g_co2);
    Serial.println(" ppm");

    Serial.print("Ap suat BME280: ");
    Serial.print(g_pressure);
    Serial.println(" hPa");

    Serial.print("Gia tri khi MQ-4: ");
    Serial.println(g_gasValue);

    Serial.print("Do am dat: ");
    Serial.print(g_soilMoisture);
    Serial.println(" (0-1023)");

    delay(3000);
  } else {
    Serial.println("Du lieu CO2 chua san sang, bo qua lan doc nay.");
    delay(1000);
  }
  Serial.print("Activated");
}

void deactivate_System() {
  systemActive = false;
  open_System();
  Serial.print("Deactivated");

}

void sms_sent_callback() {
  if (g_dataReady) {
    String message = "information at n times:\n";
    message += "Temp: " + String(g_temperature) + " C\n";
    message += "Hum: " + String(g_humidity) + " %\n";
    message += "Gas: " + String(g_gasValue) + "\n";
    message += "Soil: " + String(g_soilMoisture) + "%\n";
    message += "Light: " + String(g_lux) + " Lux\n";
    message += "Pressure: " + String(g_pressure) + " hPa";
    sendSMS_Alert(phoneNumber, message);
    Serial.println("Periodic SMS alert sent.");
  }
  else{
    Serial.println("Data not ready, SMS alert not sent.");
  }

  delay(1000);
  updateSIM_Connection();
      


}

void setup() {

  Serial.begin(115200);
  delay(2000);

// i2c setup
  Wire.begin();

  // Cấu hình chân Relay và LED` 

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
  unsigned long currentMillis = millis();
  // --- Read all sensor values once per loop ---
  g_lux = readBH1750_Lux();
  readSHT30_Data(g_temperature, g_humidity);
  g_pressure = readBME280_Pressure();
  g_gasValue = readMQ4_Gas();
  g_soilMoisture = readSoil_Moisture();
  g_dataReady = isSCD40_DataReady();
  if (g_dataReady) {
    g_co2 = readSCD40_CO2();
  }

  // --- In dữ liệu ra Serial Monitor --- (control on Serial)
  if (Serial.available()){
    char command = Serial.read();
    switch (command)
    {
    case 1:
    //open door and stop system
      delay(500); // debounce
      deactivate_System();
      break;
    case 2:
    // sent message to phone number
      delay(500); // debounce
      sms_sent_callback();
      break;
    default:
    // close door and activate system
      delay(500); // debounce
      activate_System();
      break;
    }
  }

  // --- Logic control  --- (communication module)
  if (systemActive && g_dataReady) {
    control_Fan(true);
    if (currentMillis - last_period_message >= interval_message) {
      String message = "information at n times:\n";
      message += "CO2: " + String(g_co2) + " ppm\n";
      message += "Temp: " + String(g_temperature) + " C\n";
      message += "Hum: " + String(g_humidity) + " %\n";
      message += "Gas: " + String(g_gasValue) + "\n";
      message += "Soil: " + String(g_soilMoisture) + "%\n";
      message += "Light: " + String(g_lux) + " Lux\n";
      message += "Pressure: " + String(g_pressure) + " hPa";
      alertSent = true;
      sendSMS_Alert(phoneNumber, message);
      last_period_message = currentMillis;
    }
  } else {
    control_Fan(false);
    alertSent = false;
  }
  
  updateSIM_Connection();

  delay(2000); 
}