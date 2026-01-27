#include "fan.h"

void setup_Actuators() {
    pinMode(FAN_RELAY_PIN, OUTPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    
    // Mặc định tắt quạt khi khởi động (Mức CAO thường là TẮT với module Relay kích thấp)
    digitalWrite(FAN_RELAY_PIN, HIGH); 
    digitalWrite(STATUS_LED_PIN, HIGH); // Tắt LED
}

void control_Fan(bool state) {
    if (state) {
        digitalWrite(FAN_RELAY_PIN, LOW);  // BẬT Quạt (Mức thấp)
        digitalWrite(STATUS_LED_PIN, LOW); // Bật LED báo hiệu
        Serial.println(">>> QUAT: BAT");
    } else {
        digitalWrite(FAN_RELAY_PIN, HIGH); // TẮT Quạt (Mức cao)
        digitalWrite(STATUS_LED_PIN, HIGH); // Tắt LED
        Serial.println(">>> QUAT: TAT");
    }
}

void toggle_StatusLED() {
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
}