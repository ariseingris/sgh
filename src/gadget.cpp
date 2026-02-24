#include "gadget.h"

// --- CẤU HÌNH HỆ THỐNG ---
unsigned long  start_time = 0;
bool flag_system_active = false; // Biến cờ để theo dõi trạng thái hệ thống

// Active High: HIGH = ON, LOW = OFF
void setup_Actuators() {
    digitalWrite(FAN_RELAY, LOW);


    pinMode(FAN_RELAY, OUTPUT);
    pinMode(PISTON_IN1, OUTPUT);
    pinMode(PISTON_IN2, OUTPUT);
    pinMode(BUTTON_OPEN_PIN, INPUT_PULLUP);
    pinMode(BUTTON_CLOSE_PIN, INPUT_PULLUP);

    // Mặc định TẮT hết khi khởi động (Mức LOW cho Active High)
    
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
void turn_Fan_ON() {
    digitalWrite(FAN_RELAY, HIGH);
    Serial.println(">>> QUAT: BAT");
}

void turn_Fan_OFF() {
    digitalWrite(FAN_RELAY, LOW);
    Serial.println(">>> QUAT: TAT");
}


// --- LOGIC ĐÓNG/MỞ HỆ THỐNG ---
void deactivate_system() {
    Serial.println("--- DANG MO HE THONG ---");
    turn_Fan_OFF();
    extend_Piston();// Chờ xilanh chạy hết hành trình
    start_time = millis(); // Thời gian này có thể điều chỉnh tùy theo tốc độ xilanh
    flag_system_active = true;
}

void activate_system() {
    Serial.println("--- DANG DONG HE THONG ---");
    turn_Fan_ON();
    retract_Piston();
    start_time = millis(); // Thời gian này có thể điều chỉnh tùy theo tốc độ xilanh
    flag_system_active = false; 
}

void update_actuators() {
    if (flag_system_active) {
        // Nếu hệ thống đang mở, kiểm tra thời gian để đóng lại
        if (millis() - start_time >= 8000) { // 30 giây
            stop_Piston(); // Dừng xilanh sau khi đã mở đủ thời gian
            flag_system_active = false; // Reset cờ sau khi đã đóng hệ thống
        }
    }
}



// --- KIỂM TRA NÚT BẤM VẬT LÝ ---
void check_PhysicalButtons() {
    if (digitalRead(BUTTON_OPEN_PIN) == LOW) { // Nhấn nút Open
        deactivate_system();
        delay(500); // Chống dội phím      
    }
    
    if (digitalRead(BUTTON_CLOSE_PIN) == LOW) { // Nhấn nút Close
        activate_system();
        delay(500);
    }
}