#include "fan.h"

void setup_Actuators() {
    pinMode(FAN_RELAY_PIN, OUTPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);
    
    // Mặc định tắt quạt khi khởi động 
    digitalWrite(FAN_RELAY_PIN, HIGH); 
    digitalWrite(STATUS_LED_PIN, HIGH); 
}

void control_Fan(bool state) {
    if (state) {
        digitalWrite(FAN_RELAY_PIN, LOW); 
        digitalWrite(STATUS_LED_PIN, LOW); 
        Serial.println(">>> QUAT: BAT");
    } else {
        digitalWrite(FAN_RELAY_PIN, HIGH); 
        digitalWrite(STATUS_LED_PIN, HIGH);
        Serial.println(">>> QUAT: TAT");
    }
}

void toggle_StatusLED() {
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
}