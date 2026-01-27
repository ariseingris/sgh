#include "sim_modun.h"

HardwareSerial SerialSIM(PA3, PA2); 

void setupSIM_A7680() {
    SerialSIM.begin(115200); // Tốc độ mặc định của SIM A7680
    delay(2000);
    
    Serial.println("Kiem tra ket noi SIM A7680...");
    
    // Thử gửi lệnh AT cơ bản
    SerialSIM.println("AT");
    delay(500);
    if (SerialSIM.available()) {
        String resp = SerialSIM.readString();
        Serial.println("Phan hoi tu SIM: " + resp);
    }
    
    // Cấu hình chế độ tin nhắn văn bản
    SerialSIM.println("AT+CMGF=1"); 
    delay(500);
}

void sendSMS_Alert(String phoneNumber, String message) {
    Serial.println("Dang gui SMS den: " + phoneNumber);
    
    SerialSIM.print("AT+CMGS=\"");
    SerialSIM.print(phoneNumber);
    SerialSIM.println("\"");
    delay(500);
    
    SerialSIM.print(message);
    delay(100);
    SerialSIM.write(26); 
    delay(5000); 
    
    Serial.println("SMS da gui xong.");
}

void updateSIM_Connection() {
    if (SerialSIM.available()) {
        Serial.print("[SIM]: ");
        Serial.println(SerialSIM.readString());
    }
}