// ============================================================
//  sim_modun.cpp
//  SIM A7680C driver — TinyGSM + MQTT via HiveMQ
//  Board: STM32 Blue Pill (bluepill_f103c8)
// ============================================================

#include "sim_modun.h"
#include "gadget.h"  // For activate_system() / deactivate_system() via MQTT command

// -------------------------------------------------------
//  Hardware Serial — USART1 on Blue Pill
//  HardwareSerial(RX_PIN, TX_PIN)
//  PA10 = RX (receives from SIM TX line)
//  PA9  = TX (sends to SIM RX line)
// -------------------------------------------------------
HardwareSerial SerialSIM(SIM_RX_PIN, SIM_TX_PIN); // PA10, PA9

// -------------------------------------------------------
//  TinyGSM objects
// -------------------------------------------------------
TinyGsm       modem(SerialSIM);
TinyGsmClient gsmClient(modem);         // Plain TCP — used by MQTT
PubSubClient  mqttClient(gsmClient);    // MQTT over the GSM TCP client

// -------------------------------------------------------
//  Internal state
// -------------------------------------------------------
static bool gprsConnected     = false;
static unsigned long lastMqttReconnectAttempt = 0;

// ============================================================
//  mqttCallback()
//  Called automatically by PubSubClient when a message arrives
//  on a subscribed topic (MQTT_TOPIC_CONTROL).
//
//  Supported commands (send from HiveMQ Websocket client):
//    "SYSTEM_ON"   → activate system  (fan on, piston retract)
//    "SYSTEM_OFF"  → deactivate system (fan off, piston extend)
//    "FAN_ON"      → turn fan on only
//    "FAN_OFF"     → turn fan off only
//    "PISTON_OPEN" → extend piston only
//    "PISTON_CLOSE"→ retract piston only
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (unsigned int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }
    msg.trim();

    Serial.print("[MQTT] Command received on topic '");
    Serial.print(topic);
    Serial.print("': ");
    Serial.println(msg);

    // --- Parse and execute commands ---
    if (msg == "SYSTEM_ON") {
        activate_system();
        Serial.println("[MQTT] -> He thong DONG (Fan ON, Piston retract)");

    } else if (msg == "SYSTEM_OFF") {
        deactivate_system();
        Serial.println("[MQTT] -> He thong MO (Fan OFF, Piston extend)");

    } else if (msg == "FAN_ON") {
        turn_Fan_ON();
        Serial.println("[MQTT] -> Quat BAT");

    } else if (msg == "FAN_OFF") {
        turn_Fan_OFF();
        Serial.println("[MQTT] -> Quat TAT");

    } else if (msg == "PISTON_OPEN") {
        extend_Piston();
        Serial.println("[MQTT] -> Xilanh DAY RA");

    } else if (msg == "PISTON_CLOSE") {
        retract_Piston();
        Serial.println("[MQTT] -> Xilanh RUT VE");

    } else {
        Serial.println("[MQTT] -> Lenh khong nhan ra, bo qua.");
    }
}

// ============================================================
//  connectMQTT()
//  Blocking connect with retries. Called from setupSIM_A7680()
//  and also non-blocking retried from loopMQTT().
// ============================================================
void connectMQTT() {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512); // Default 256 is too small for sensor JSON

    Serial.print("[MQTT] Dang ket noi HiveMQ broker: ");
    Serial.print(MQTT_BROKER);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    // Try to connect (will block up to ~5s per attempt)
    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
        Serial.println("[MQTT] Ket noi thanh cong!");

        // Subscribe to control topic so we receive commands from web/dashboard
        mqttClient.subscribe(MQTT_TOPIC_CONTROL);
        Serial.println("[MQTT] Da subscribe topic: " MQTT_TOPIC_CONTROL);

        // Publish a hello message so you can verify on HiveMQ Websocket client
        mqttClient.publish(MQTT_TOPIC_SENSORS, "{\"status\":\"online\",\"device\":\"STM32_A7680C\"}");

    } else {
        Serial.print("[MQTT] Ket noi that bai, ma loi: ");
        Serial.println(mqttClient.state());
        // State codes: -4=timeout, -3=connection lost, -2=connect failed,
        //              -1=disconnected, 1=bad protocol, 2=bad client id,
        //              3=server unavailable, 4=bad credentials, 5=unauthorized
    }
}

// ============================================================
//  setupSIM_A7680()
// ============================================================
void setupSIM_A7680() {
    SerialSIM.begin(SIM_BAUD);
    delay(1000);

    Serial.println("[SIM] Khoi dong SIM A7680C...");

    // Restart modem and wait for it to settle
    // modem.restart() sends ATZ, AT, waits for OK internally
    modem.restart();
    delay(5000); // A7680C needs ~3-5s after restart before accepting commands

    // Print modem info for verification
    String info = modem.getModemInfo();
    Serial.print("[SIM] Modem: ");
    Serial.println(info.length() > 0 ? info : "Khong doc duoc thong tin");

    String imei = modem.getIMEI();
    Serial.print("[SIM] IMEI: ");
    Serial.println(imei.length() > 0 ? imei : "Khong doc duoc IMEI");

    // Set SMS text mode for sendSMS_Alert()
    modem.sendAT(GF("+CMGF=1"));
    modem.waitResponse(1000);

    // --- Wait for GSM network registration ---
    Serial.println("[SIM] Doi mang GSM (toi da 60s)...");
    if (!modem.waitForNetwork(SIM_NETWORK_TIMEOUT_MS)) {
        Serial.println("[SIM] LOI NGHIEM TRONG: Khong tim thay mang GSM!");
        Serial.println("[SIM] Kiem tra: SIM co song khong? Antenna cham chưa?");
        return;
    }
    Serial.println("[SIM] Mang GSM OK.");
    Serial.print("[SIM] Chat luong song: ");
    Serial.println(modem.getSignalQuality()); // 0-31, higher=better, 99=unknown

    // --- Attach GPRS ---
    Serial.print("[SIM] Ket noi GPRS (APN: ");
    Serial.print(SIM_APN);
    Serial.println(")...");

    if (modem.gprsConnect(SIM_APN, SIM_APN_USER, SIM_APN_PASS)) {
        gprsConnected = true;
        Serial.print("[SIM] GPRS OK. IP: ");
        Serial.println(modem.getLocalIP());
    } else {
        gprsConnected = false;
        Serial.println("[SIM] LOI: Khong ket noi duoc GPRS!");
        Serial.println("[SIM] SMS van hoat dong, nhung MQTT se that bai.");
        return; // Cannot do MQTT without GPRS
    }

    // --- Connect to MQTT broker ---
    connectMQTT();

    Serial.println("[SIM] Setup hoan tat.");
}

// ============================================================
//  maintainGPRS()
//  Call every GPRS_CHECK_INTERVAL ms in loop()
// ============================================================
void maintainGPRS() {
    if (!modem.isGprsConnected()) {
        gprsConnected = false;
        Serial.println("[SIM] GPRS mat ket noi. Dang ket noi lai...");
        if (modem.gprsConnect(SIM_APN, SIM_APN_USER, SIM_APN_PASS)) {
            gprsConnected = true;
            Serial.print("[SIM] GPRS ket noi lai OK. IP: ");
            Serial.println(modem.getLocalIP());
        } else {
            Serial.println("[SIM] GPRS ket noi lai that bai.");
        }
    }
}

// ============================================================
//  loopMQTT()
//  NON-BLOCKING — call every loop()
//  Handles PINGREQ keepalive and incoming command messages
// ============================================================
void loopMQTT() {
    if (!gprsConnected) return;

    if (!mqttClient.connected()) {
        unsigned long now = millis();
        // Non-blocking retry: only try reconnect after MQTT_RECONNECT_INTERVAL
        if (now - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
            lastMqttReconnectAttempt = now;
            Serial.println("[MQTT] Bi ngat ket noi. Dang thu ket noi lai...");
            connectMQTT();
        }
        return;
    }

    mqttClient.loop(); // Process incoming messages and send keepalive
}

// ============================================================
//  publishSensorMQTT()
//  Publishes a JSON payload to MQTT_TOPIC_SENSORS
//
//  Example payload:
//  {"temp":28.5,"hum":65.2,"co2":412,"lux":1200.0,
//   "pressure":1013.2,"gas":320,"soil":540}
//
//  Monitor live at: https://www.hivemq.com/demos/websocket-client/
//  → Host: broker.hivemq.com, Port: 8884 (WSS)
//  → Subscribe to: greenhouse/stm32/sensors
// ============================================================
void publishSensorMQTT(float temp, float hum, uint16_t co2,
                       float lux, float pressure, int gas, int soil) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Chua ket noi broker, bo qua publish.");
        return;
    }

    // Build compact JSON (avoid String + in a tight loop for memory safety)
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"temp\":%.1f,\"hum\":%.1f,\"co2\":%u,\"lux\":%.1f,\"pressure\":%.1f,\"gas\":%d,\"soil\":%d}",
        temp, hum, co2, lux, pressure, gas, soil
    );

    bool ok = mqttClient.publish(MQTT_TOPIC_SENSORS, payload);

    if (ok) {
        Serial.print("[MQTT] Published: ");
        Serial.println(payload);
    } else {
        Serial.println("[MQTT] Publish that bai (buffer day hoac mat ket noi).");
    }
}

// ============================================================
//  clearSIMBuffer()
// ============================================================
void clearSIMBuffer() {
    while (SerialSIM.available()) {
        SerialSIM.read();
    }
}

// ============================================================
//  updateSIM_Connection()
//  Forward unsolicited modem messages (URC) to Serial monitor.
//  Call every loop() — fast, non-blocking.
// ============================================================
void updateSIM_Connection() {
    if (SerialSIM.available()) {
        String incoming = SerialSIM.readStringUntil('\n');
        incoming.trim();
        if (incoming.length() > 0) {
            Serial.print("[SIM URC] ");
            Serial.println(incoming);
        }
    }
}

// ============================================================
//  sendSMS_Alert()
//  Raw AT command SMS (TinyGSM's sendSMS uses blocking delay
//  which can cause watchdog issues on long payloads)
// ============================================================
void sendSMS_Alert(const String& phoneNumber, const String& message) {
    Serial.println("[SIM] Gui SMS den: " + phoneNumber);

    // Set text mode
    modem.sendAT(GF("+CMGF=1"));
    if (modem.waitResponse(1000) != 1) {
        Serial.println("[SIM] LOI: Khong dat duoc Text Mode.");
        return;
    }

    // Send phone number command
    SerialSIM.print("AT+CMGS=\"");
    SerialSIM.print(phoneNumber);
    SerialSIM.print("\"\r"); // \r only — NOT println(), the extra \n breaks some firmware

    // Wait for '>' prompt
    unsigned long start = millis();
    bool gotPrompt = false;
    while (millis() - start < 5000) {
        if (SerialSIM.available() && SerialSIM.read() == '>') {
            gotPrompt = true;
            break;
        }
    }

    if (!gotPrompt) {
        Serial.println("[SIM] LOI: Khong nhan '>' prompt. Gui ESC de thoat.");
        SerialSIM.write(27); // ESC - exit stuck state
        delay(200);
        clearSIMBuffer();
        return;
    }

    // Send body + Ctrl+Z to submit
    SerialSIM.print(message);
    delay(100);
    SerialSIM.write(26); // Ctrl+Z = end of message

    // Wait for +CMGS confirmation
    unsigned long waitStart = millis();
    String response = "";
    while (millis() - waitStart < SMS_SEND_TIMEOUT_MS) {
        if (SerialSIM.available()) {
            char c = SerialSIM.read();
            response += c;
            if (response.indexOf("+CMGS") >= 0 || response.indexOf("OK") >= 0) {
                Serial.println("[SIM] SMS gui thanh cong.");
                return;
            }
            if (response.indexOf("ERROR") >= 0) {
                Serial.println("[SIM] LOI SMS: " + response);
                return;
            }
        }
    }
    Serial.println("[SIM] Timeout: Khong co phan hoi SMS.");
}