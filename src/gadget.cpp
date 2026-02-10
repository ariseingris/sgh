#include "gadget.h"

// --- CẤU HÌNH HỆ THỐNG ---
// Active High: HIGH = ON, LOW = OFF
void setup_Actuators() {
    pinMode(FAN_RELAY, OUTPUT);
    pinMode(PISTON_IN1, OUTPUT);
    pinMode(PISTON_IN2, OUTPUT);
    pinMode(BUTTON_OPEN_PIN, INPUT_PULLUP);
    pinMode(BUTTON_CLOSE_PIN, INPUT_PULLUP);

    // Mặc định TẮT hết khi khởi động (Mức LOW cho Active High)
    digitalWrite(FAN_RELAY, LOW);
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, LOW);
}

// --- ĐIỀU KHIỂN XILANH (Sử dụng cầu H) ---
void extend_Piston() {
    digitalWrite(PISTON_IN1, HIGH);  
    digitalWrite(PISTON_IN2, LOW); 
    Serial.println(">>> XILANH: DAY RA (EXTEND)");
}

void retract_Piston() {
    digitalWrite(PISTON_IN1, LOW);  
    digitalWrite(PISTON_IN2, HIGH); 
    Serial.println(">>> XILANH: RUT VE (RETRACT)");
}

void stop_Piston() {
    digitalWrite(PISTON_IN1, LOW);  
    digitalWrite(PISTON_IN2, LOW); 
    Serial.println(">>> XILANH: DUNG");
}

// --- ĐIỀU KHIỂN QUẠT ---
void control_Fan(bool state) {
    if (state) {
        digitalWrite(FAN_RELAY, HIGH); // Kích Relay ON
        Serial.println(">>> QUAT: BAT");
    } else {
        digitalWrite(FAN_RELAY, LOW); // Kích Relay OFF
        Serial.println(">>> QUAT: TAT");
    }
}

// --- LOGIC ĐÓNG/MỞ HỆ THỐNG ---
void open_System() {
    Serial.println("--- DANG MO HE THONG ---");
    extend_Piston();
    delay(2000); // Chờ xilanh chạy hết hành trình
    stop_Piston();
    control_Fan(true); // Mở xong thì bật quạt để thông gió
}
}

void close_System() {
    Serial.println("--- DANG DONG HE THONG ---");
    control_Fan(true); // Tắt quạt trước khi đóng
    delay(500);
    retract_Piston();
    delay(2000);
    stop_Piston();
}
}



// --- KIỂM TRA NÚT BẤM VẬT LÝ ---
void check_PhysicalButtons() {
    if (digitalRead(BUTTON_OPEN_PIN) == LOW) { // Nhấn nút Open
        open_System();
        delay(500); // Chống dội phím      
    }
    
    if (digitalRead(BUTTON_CLOSE_PIN) == LOW) { // Nhấn nút Close
        close_System();
        delay(500);
    }
}