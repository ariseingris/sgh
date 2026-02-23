#include <Arduino.h>
#include <Wire.h>
#include "sensor_light.h"    // BH1750
#include "sensor_temp_hum.h" // SHT31
#include "sensor_co2.h"      // SCD40
#include "sensor_pressure.h" // BME280
#include "sensor_gas.h"      // MQ-4 (Analog)
#include "sensor_soil.h"     // Soil Moisture (Analog)  
#include "sim_modun.h"       // SIM A7680

// ===== PIN DEFINITIONS =====
#define RELAY_X1 PA3
#define RELAY_X2 PA4
const int RELAY_FAN = PA1;

// ===== SIM SETUP =====
const String phoneNumber = "+84814686688"; // alert phone number

// ===== TIMING VARIABLES =====
unsigned long last_test_action = 0;
const unsigned long interval_test_action = 10000;    // Test interval (10 seconds)
unsigned long last_sensor_read = 0;
const unsigned long interval_sensor_read = 2000;     // Sensor read interval (2 seconds)

// ===== SYSTEM STATE =====
bool systemActive = false;
bool systemInitialized = false;

// ===== GLOBAL SENSOR VALUES =====
float g_lux = 0.0;
float g_temperature = 0.0;
float g_humidity = 0.0;
float g_pressure = 0.0;
int g_gasValue = 0;
int g_soilMoisture = 0;
uint16_t g_co2 = 0;
bool g_dataReady = false;

// ===== ACTUATOR STATE MACHINE (NON-BLOCKING) =====
enum ActuatorState { STOPPED, RETRACTING, EXTENDING };
ActuatorState currentActuatorState = STOPPED;
unsigned long actuatorMoveStartTime = 0;
const unsigned long ACTUATOR_RUN_TIME = 1000; // Thời gian chạy của Piston (1 giây)

void printSensorData();

void stopActuator() {
  digitalWrite(RELAY_X1, LOW);
  digitalWrite(RELAY_X2, LOW);
  currentActuatorState = STOPPED;
}

void triggerRetractActuator() {
  Serial.println("[ACTUATOR] Dang thu Pittong vao...");
  digitalWrite(RELAY_X1, LOW);
  digitalWrite(RELAY_X2, HIGH);
  currentActuatorState = RETRACTING;
  actuatorMoveStartTime = millis();
}

void triggerExtendActuator() {
  Serial.println("[ACTUATOR] Dang day Pittong ra...");
  digitalWrite(RELAY_X1, HIGH); 
  digitalWrite(RELAY_X2, LOW);
  currentActuatorState = EXTENDING;
  actuatorMoveStartTime = millis();
}

// Hàm này phải được gọi liên tục trong loop() để theo dõi thời gian Pittong chạy
void handleActuatorTask() {
  if (currentActuatorState != STOPPED) {
    if (millis() - actuatorMoveStartTime >= ACTUATOR_RUN_TIME) {
      stopActuator(); // Đủ 1 giây thì tự động dừng
      Serial.println("[ACTUATOR] Da hoan thanh va dung lai.");
    }
  }
}

// ===== SYSTEM ACTIVATION/DEACTIVATION =====
void activate_System() {
  if (systemActive) return;  
  
  systemActive = true;
  Serial.println("\n----- KICH HOAT HE THONG (TEST MODE) -----");
  
  // Kích hoạt quạt và thu Pittong
  digitalWrite(RELAY_FAN, HIGH);
  triggerRetractActuator(); 
}

void deactivate_System() {
  if (!systemActive) return;  
  
  systemActive = false;
  Serial.println("\n----- TAT HE THONG (TEST MODE) -----");
  
  // Tắt quạt và đẩy Pittong
  digitalWrite(RELAY_FAN, LOW);
  triggerExtendActuator();
}

void printSensorData() {
  Serial.println("--- Du lieu cam bien ---");
  Serial.print("Anh sang: "); Serial.print(g_lux); Serial.println(" Lux");
  Serial.print("Nhiet do: "); Serial.print(g_temperature); Serial.println(" C");
  Serial.print("Do am: "); Serial.print(g_humidity); Serial.println(" %");
  Serial.print("CO2 SCD40: "); Serial.print(g_co2); Serial.println(" ppm");
  Serial.print("Ap suat: "); Serial.print(g_pressure); Serial.println(" hPa");
  Serial.print("Khi Gas: "); Serial.println(g_gasValue);
  Serial.print("Do am dat: "); Serial.print(g_soilMoisture); Serial.println(" %");
  Serial.println("------------------------");
}

// ===== SENSOR DATA READING FUNCTION =====
void readAllSensors() {
  unsigned long currentMillis = millis();
  
  // Chỉ đọc sensor sau mỗi 2 giây để tránh kẹt Bus I2C
  if (currentMillis - last_sensor_read < interval_sensor_read) {
    return;
  }
  last_sensor_read = currentMillis;
  
  // Đọc Analog
  g_gasValue = readMQ4_Gas();
  g_soilMoisture = readSoil_Moisture();
  
  // Đọc I2C
  g_lux = readBH1750_Lux();
  readSHT30_Data(g_temperature, g_humidity);
  g_pressure = readBME280_Pressure();
  
  // Đọc CO2 (Lấy luôn dữ liệu không cần ép điều kiện logic cho logic chính)
  g_dataReady = isSCD40_DataReady();
  if (g_dataReady) {
    g_co2 = readSCD40_CO2();
  }
}

// ===== SMS ALERT FUNCTION =====
void sendSensorDataSMS() {
  String message = "TEST MODE - Greenhouse DATA:\n";
  message += "Temp: " + String(g_temperature) + "C\n";
  message += "Hum: " + String(g_humidity) + "%\n";
  message += "CO2: " + String(g_co2) + "ppm\n";
  message += "Light: " + String(g_lux) + "Lx\n";
  message += "Gas: " + String(g_gasValue);
  
  sendSMS_Alert(phoneNumber, message);
  Serial.println("[SMS] Da gui tin nhan test he thong.");
}

// ===== SETUP FUNCTION =====
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  // Setup Fan & Actuator Pins
  pinMode(RELAY_FAN, OUTPUT);
  digitalWrite(RELAY_FAN, LOW);
  
  pinMode(RELAY_X1, OUTPUT);
  pinMode(RELAY_X2, OUTPUT);
  stopActuator();
  
  // Setup I2C
  Wire.begin();
  Serial.println("\n========== SMART GREENHOUSE (TEST MODE) ==========");
  Serial.println("DANG KHOI TAO CAM BIEN...");
  
  setupBH1750_Sensor(); delay(100);
  setupSHT30_Sensor();  delay(100);
  setupSCD40_Sensor();  delay(100);
  setupBME280_Sensor(); delay(100);
  
  Serial.println("DANG KHOI TAO SIM MODULE...");
  setupSIM_A7680();
  
  Serial.println("HE THONG DA SAN SANG!");
  Serial.println("==================================================");
  
  systemInitialized = true;
  systemActive = false;
  last_test_action = millis(); // Bắt đầu đếm thời gian test
}

// ===== MAIN LOOP =====
void loop() {
  unsigned long currentMillis = millis();
  
  // 1. Luôn luôn cập nhật cảm biến (không bao giờ bị kẹt)
  readAllSensors();
  
  // 2. Cập nhật trạng thái Pittong (Tự động ngắt sau 1s)
  handleActuatorTask();

  
  // 3. LOGIC TEST: Cứ đúng 10 giây đảo trạng thái và gửi thông tin 1 lần (Bỏ qua CO2)
  if (systemInitialized) {
    if (currentMillis - last_test_action >= interval_test_action) {
      last_test_action = currentMillis;
      
      if (!systemActive) {
        activate_System();
        sendSensorDataSMS(); // Gửi khi BẬT
      } else {
        deactivate_System();
        sendSensorDataSMS(); // Gửi khi TẮT
      }
    }
  }

  printSensorData(); // In dữ liệu cảm biến liên tục để theo dõi (bỏ qua CO2 nếu chưa sẵn sàng)
  
  // 4. CẬP NHẬT MODULE SIM
  updateSIM_Connection();
  
  // Delay cực nhỏ 10ms để tránh watchdog reset (không ảnh hưởng hệ thống)
  delay(10);
}