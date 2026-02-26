#include "sim_modun.h"

HardwareSerial SerialSIM(PA9, PA10); // RX, TX pins for SIM A7680

void setupSIM_A7680() {
    SerialSIM.begin(115200); 
    Serial.println("Cho SIM A7680 khoi dong va bat song...");    
    
    // Vòng lặp "gọi hồn" - Ép module nhận Baudrate và thức dậy
    bool simReady = false;
    for (int i = 0; i < 30; i++) { // Thử tối đa 30 lần ~ 15 giây
        SerialSIM.println("AT");
        delay(500);
        if (SerialSIM.available()) {
            String resp = SerialSIM.readString();
            if (resp.indexOf("OK") >= 0) {
                simReady = true;
                Serial.println("SIM da phan hoi OK! Giao tiep thanh cong.");
                break;
            }
        }
    }
    
    if (!simReady) {
        Serial.println("LOI NGHANG: Khong thay SIM tra loi. Kiem tra TX/RX hoac Nguon!");
        return; // Dừng luôn, không cho chạy tiếp
    }
        
    // Cấu hình chế độ tin nhắn văn bản
    SerialSIM.println("AT+CMGF=1"); 
    delay(500);
    clearSIMBuffer();
}

void clearSIMBuffer() {
    while (SerialSIM.available()) {
        SerialSIM.read();
    }
}

void sendSMS_Alert(String phoneNumber, String message) {
    Serial.println("Dang gui SMS den: " + phoneNumber);
    
    // Đảm bảo module ở chế độ Text
    SerialSIM.println("AT+CMGF=1");
    delay(500); // Tăng delay để module kịp xử lý lệnh
    clearSIMBuffer();
    
    // Bắt đầu gửi lệnh cấu hình số điện thoại
    SerialSIM.print("AT+CMGS=\"");
    SerialSIM.print(phoneNumber);
    SerialSIM.print("\"\r"); // QUAN TRỌNG: Chỉ dùng \r, không dùng println() để tránh dư ký tự \n
    
    // Đợi ký tự '>' một cách an toàn (timeout 5s) - Dùng Non-blocking
    unsigned long startTime = millis();
    bool promptReceived = false;
    
    while (millis() - startTime < 5000) {
        if (SerialSIM.available()) {
            char c = SerialSIM.read(); // Đọc từng ký tự thay vì dùng find()
            Serial.print(c); // In ra Serial để bạn dễ debug
            if (c == '>') {
                promptReceived = true;
                break;
            }
        }
    }

    if (promptReceived) {
        Serial.println("\nDa nhan '>', dang gui noi dung...");
        SerialSIM.print(message);
        delay(100);
        SerialSIM.write(26); // Mã ASCII của Ctrl+Z để chốt và gửi
        Serial.println("Da gui thong diep. Cho phan hoi tu mang (toi da 15s)...");
        
        // Chờ OK hoặc ERROR (Đọc từng ký tự cho mượt)
        unsigned long waitStart = millis();
        bool done = false;
        String response = "";
        
        while (millis() - waitStart < 15000) { // Tăng lên 15s vì gửi SMS đôi khi chậm do mạng
            if (SerialSIM.available()) {
                char c = SerialSIM.read();
                response += c; // Cộng dồn ký tự
                
                // Kiểm tra xem chuỗi phản hồi đã có OK, +CMGS hoặc ERROR chưa
                if (response.indexOf("OK") >= 0 || response.indexOf("+CMGS") >= 0) {
                    Serial.println("\nSMS da gui thanh cong.");
                    done = true;
                    break;
                } else if (response.indexOf("ERROR") >= 0) {
                    Serial.println("\nLoi khi gui SMS: " + response);
                    done = true;
                    break;
                }
            }
        }
        
        if(!done) {
            Serial.println("\nTimeout: Khong co phan hoi tu mang sau khi gui.");
        }
    } else {
        Serial.println("\nLoi: Khong nhan duoc '>'. Huy gui de tranh treo may.");
        SerialSIM.write(27); // Gửi phím ESC (Mã ASCII 27) để thoát khỏi trạng thái kẹt
        delay(200);
        clearSIMBuffer();
    }
}

void updateSIM_Connection() {
    if (SerialSIM.available()) {
        Serial.print("[SIM]: ");
        Serial.println(SerialSIM.readString());
    }
}