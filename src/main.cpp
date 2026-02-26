#include <Arduino.h>
#include <Wire.h>
#include "sensor_light.h"    // BH1750
#include "sensor_temp_hum.h" // SHT31
#include "sensor_co2.h"      // SCD40
#include "sensor_pressure.h" // BME280
#include "sensor_gas.h"      // MQ-4 (Analog)
#include "sensor_soil.h"     // Soil Moisture (Analog)  
#include "sim_modun.h"       // SIM A7680
#include "gadget.h"          // Actuators: Fan, Piston

// sim pre setup
const String phoneNumber = "+84812065252"; // alert phone number
unsigned long last_period_message = 0;
const unsigned long interval_message = 3600000;
bool alertSent = false;
bool systemActive = false; // Global system state

// time control for actuators & printing
unsigned long previousMillis = 0;
bool actuator_state = false; // false: closed, true: open

unsigned long previousPrintMillis = 0;
const long printInterval = 5000; // 5 seconds for printing

// ADDED: Timer for reading sensors safely to prevent I2C bus lockup
unsigned long previousSensorMillis = 0;
const long sensorInterval = 5000; // Read sensors every 2 seconds

// Global sensor values - read once per loop
float g_lux = 0.0;
float g_temperature = 0.0;
float g_humidity = 0.0;
float g_pressure = 0.0;
int g_gasValue = 0;
int g_soilMoisture = 0;
uint16_t g_co2 = 0;
bool g_dataReady = false;

void printdata(){
    Serial.println("----- Du lieu cam bien -----");
    Serial.print("Ap suat BME280: ");
    Serial.print(g_pressure);
    Serial.println(" hPa");

    Serial.print("Gia tri khi MQ-4: ");
    Serial.println(g_gasValue);

    Serial.print("Do am dat: ");
    Serial.print(g_soilMoisture);
    Serial.println(" (0-1023)");

    Serial.print("Anh sang: ");
    Serial.print(g_lux);
    Serial.println(" Lux");

    Serial.print("Nhiet do SHT30: ");
    Serial.print(g_temperature);
    Serial.println(" C");

    Serial.print("Do am SHT30: ");
    Serial.print(g_humidity);
    Serial.println(" %");

  if(g_dataReady) {
    Serial.print("CO2 SCD40: ");
    Serial.print(g_co2);
    Serial.println(" ppm");
  } else {
    Serial.println("Du lieu CO2 chua san sang, bo qua lan doc nay.");
  }
}


void sms_sent_callback() {
  String message = "information at n times:\n";
  message += "Temp: " + String(g_temperature) + " C\n";
  message += "Hum: " + String(g_humidity) + " %\n";
  message += "Gas: " + String(g_gasValue) + "\n";
  message += "Soil: " + String(g_soilMoisture) + "%\n";
  message += "Light: " + String(g_lux) + " Lux\n";
  message += "Pressure: " + String(g_pressure) + " hPa\n";
  
  if (g_dataReady) {
    message += "CO2: " + String(g_co2) + " ppm";
  } else {
    message += "CO2: Not Ready";
  }

  sendSMS_Alert(phoneNumber, message);
  Serial.println("Periodic SMS alert sent.");
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  // i2c setup
  Wire.begin();
  Serial.println(" DANG KHOI TAO HE THONG CAM BIEN ");
  setupBH1750_Sensor();
  setupSHT30_Sensor();
  setupSCD40_Sensor();
  setupBME280_Sensor();
  setup_Actuators(); // Setup actuators (Fan, Piston)
  setupSIM_A7680(); // Setup SIM module
  
  Serial.println(" HE THONG DA SAN SANG ");
  stop_Piston();
}

void loop() {
  unsigned long currentMillis = millis();

  // --- Read all sensor values safely on an interval (prevents freezing) ---
  if (currentMillis - previousSensorMillis >= sensorInterval) {
    previousSensorMillis = currentMillis;
    
    g_lux = readBH1750_Lux();
    readSHT30_Data(g_temperature, g_humidity);
    g_pressure = readBME280_Pressure();
    g_gasValue = readMQ4_Gas();
    g_soilMoisture = readSoil_Moisture();
    g_dataReady = isSCD40_DataReady();
    if (g_dataReady) {
      g_co2 = readSCD40_CO2();
    }
  }

  // --- Handle Serial Commands Instantly (Removed slow parseInt) ---
  if (Serial.available() > 0) {
    char command = Serial.read(); // Read single character instantly
    
    // Clear any remaining characters like \r or \n in the buffer
    delay(10); 
    while (Serial.available() > 0) { Serial.read(); }
    
    switch (command){
      case '1':
        systemActive = true;
        Serial.println("System Activated via Serial Command");
        break;
      case '2':
        systemActive = false;
        Serial.println("System Deactivated via Serial Command");
        break;
      case '3':
        printdata();
        Serial.println("Data Printed via Serial Command");
        break;
      case '4':
        sms_sent_callback();
        updateSIM_Connection();
        Serial.println("SMS Alert Sent via Serial Command");
        break;
      default:
        break;
    }
  }

  // --- Handle State Changes ---
  if (systemActive != actuator_state) {
    if (systemActive) {
      activate_system();
    } else {
      deactivate_system();
    }
    actuator_state = systemActive;
  }

  // --- Print data periodically ---
  if (currentMillis - previousPrintMillis >= printInterval) {
    previousPrintMillis = currentMillis;
    printdata(); 
  }

  update_actuators(); // Cập nhật trạng thái của các thiết bị chấp hành (Fan, Piston)

  // --- Logic control (communication module) ---
  if (systemActive && g_dataReady) {
    if (currentMillis - last_period_message >= interval_message) {
      sms_sent_callback();
      last_period_message = currentMillis;
      alertSent = true;
    }
  } else {
    alertSent = false;
  }
  
  updateSIM_Connection();
}