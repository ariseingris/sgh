#include "gadget.h"

void setup_Actuators() {
    pinMode(FAN_RELAY_PIN, OUTPUT);
    pinMode(PISTON_IN1_PIN, OUTPUT);
    pinMode(PISTON_IN2_PIN, OUTPUT);
    pinMode(STATUS_LED_PIN, OUTPUT);

    pinMode(BUTTON_OPEN_PIN, INPUT_PULLUP);
    pinMode(BUTTON_CLOSE_PIN, INPUT_PULLUP);

    // Mặc định TẮT hết (Mức CAO cho Relay kích thấp)
    digitalWrite(FAN_RELAY_PIN, HIGH);
    digitalWrite(PISTON_IN1_PIN, HIGH);
    digitalWrite(PISTON_IN2_PIN, HIGH);
    digitalWrite(STATUS_LED_PIN, HIGH);
}
void control_Piston(int mode) {
    if (mode == 1) { // Đẩy ra
        digitalWrite(PISTON_IN1_PIN, LOW);
        digitalWrite(PISTON_IN2_PIN, HIGH);
        Serial.println("PISTON: DANG DAY RA");
    } 
    else if (mode == 2) { // Thu vào
        digitalWrite(PISTON_IN1_PIN, HIGH);
        digitalWrite(PISTON_IN2_PIN, LOW);
        Serial.println("PISTON: DANG THU VAO");
    } 
    else { // Dừng
        digitalWrite(PISTON_IN1_PIN, HIGH);
        digitalWrite(PISTON_IN2_PIN, HIGH);
        Serial.println("PISTON: DUNG");
    }
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

void check_PhysicalButtons() {
    // Nếu bấm nút Mở (nối GND nên mức LOW là đang bấm)
    if (digitalRead(BUTTON_OPEN_PIN) == LOW) {
        control_Piston(1); // Đẩy ra
        delay(200);       
    }
    // Nếu bấm nút Đóng
    else if (digitalRead(BUTTON_CLOSE_PIN) == LOW) {
        control_Piston(2); // Thu vào
        delay(200);
    }
}

void toggle_StatusLED() {
    digitalWrite(STATUS_LED_PIN, !digitalRead(STATUS_LED_PIN));
}