#ifndef SENSOR_SIM_H
#define SENSOR_SIM_H

#include <Arduino.h>

// Khai báo các hàm quản lý SIM
void setupSIM_A7680();
void updateSIM_Connection();
void sendSMS_Alert(String phoneNumber, String message);
void sendDataToThinkSpeak(float temp, float hum, uint16_t co2); // Ví dụ gửi lên Cloud

#endif