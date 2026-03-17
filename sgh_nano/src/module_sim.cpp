// ============================================================
//  sim_modun.cpp
//  SIM A7680C driver — TinyGSM + MQTT via HiveMQ
//  Board: Arduino Nano
//
//  Architecture:
//    [SIM A7680C] ──TX/RX──► [Nano D0/D1  Hardware Serial]
//                             [Nano D10/D11 SoftwareSerial] ──► [STM32]
//
//  MQTT commands received from broker are forwarded to STM32
//  via SoftwareSerial instead of calling gadget.h directly.
// ============================================================

#include "module_sim.h"

// -------------------------------------------------------
//  SIM A7680C uses Hardware Serial (D0/D1) = "Serial"
//  on Arduino Nano.  Serial is also the USB debug port,
//  so debug prints are sent to STM32 via SerialSTM32.
// -------------------------------------------------------
#define SerialSIM Serial   // Hardware UART → A7680C

// -------------------------------------------------------
//  SoftwareSerial → STM32
// -------------------------------------------------------
SoftwareSerial SerialSTM32(STM32_SW_RX, STM32_SW_TX);  // D10=RX, D11=TX

// -------------------------------------------------------
//  TinyGSM — feed it the hardware serial port
// -------------------------------------------------------
TinyGsm       modem(SerialSIM);
TinyGsmClient gsmClient(modem);
PubSubClient  mqttClient(gsmClient);

// -------------------------------------------------------
//  Internal state
// -------------------------------------------------------
static bool gprsConnected = false;
static unsigned long lastMqttReconnectAttempt = 0;

// ============================================================
//  sendCommandToSTM32()
//  Sends a newline-terminated command to STM32 via SoftSerial.
// ============================================================
void sendCommandToSTM32(const String& cmd) {
    SerialSTM32.println(cmd);   // println adds \r\n — STM32 trims and parses
    // Optional local debug via SerialSTM32 itself is not possible here
    // since it is the STM32 link; use a logic analyser if needed.
}

// ============================================================
//  readSTM32Response()
//  Non-blocking read of any line sent back by STM32.
//  STM32 can send "ACK:<cmd>" or "ERR:<reason>" lines.
// ============================================================
void readSTM32Response() {
    while (SerialSTM32.available()) {
        String line = SerialSTM32.readStringUntil('\n');
        line.trim();
        if (line.length() > 0) {
            // We cannot use Serial here (it goes to SIM), so silently discard
            // or store in a buffer if you want to relay to MQTT later.
            // To debug: publish it to MQTT as a status message.
            if (mqttClient.connected()) {
                char buf[64];
                snprintf(buf, sizeof(buf), "{\"stm32_ack\":\"%s\"}", line.c_str());
                mqttClient.publish("greenhouse/nano/debug", buf);
            }
        }
    }
}

// ============================================================
//  mqttCallback()
//  Receives MQTT commands → forwards to STM32 via SoftwareSerial.
//
//  Supported commands (same as original STM32 version):
//    SYSTEM_ON    SYSTEM_OFF
//    FAN_ON       FAN_OFF
//    PISTON_OPEN  PISTON_CLOSE
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (unsigned int i = 0; i < length; i++) {
        msg += (char)payload[i];
    }
    msg.trim();

    // Forward every recognised command to STM32
    if (msg == "SYSTEM_ON"    ||
        msg == "SYSTEM_OFF"   ||
        msg == "FAN_ON"       ||
        msg == "FAN_OFF"      ||
        msg == "PISTON_OPEN"  ||
        msg == "PISTON_CLOSE") {

        sendCommandToSTM32(msg);

        // Publish ACK back to broker so dashboard can confirm delivery
        if (mqttClient.connected()) {
            char ack[64];
            snprintf(ack, sizeof(ack), "{\"forwarded\":\"%s\"}", msg.c_str());
            mqttClient.publish("greenhouse/nano/ack", ack);
        }
    }
    // Unknown commands are silently dropped
}

// ============================================================
//  connectMQTT()
// ============================================================
void connectMQTT() {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);

    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
        mqttClient.subscribe(MQTT_TOPIC_CONTROL);
        mqttClient.publish(MQTT_TOPIC_SENSORS,
            "{\"status\":\"online\",\"device\":\"Nano_A7680C\"}");
    }
    // No Serial.print here — Serial is occupied by SIM.
    // Check MQTT state via publishSensorMQTT success/fail or broker dashboard.
}

// ============================================================
//  setupSIM_A7680()
// ============================================================
void setupSIM_A7680() {
    // Start the SoftSerial link to STM32 first
    SerialSTM32.begin(STM32_BAUD);
    delay(100);

    // Start hardware serial for SIM
    SerialSIM.begin(SIM_BAUD);
    delay(1000);

    modem.restart();
    delay(5000);

    // Set SMS text mode
    modem.sendAT(GF("+CMGF=1"));
    modem.waitResponse(1000);

    // Wait for GSM network
    if (!modem.waitForNetwork(SIM_NETWORK_TIMEOUT_MS)) {
        // Signal failure to STM32 so it can log/alert
        sendCommandToSTM32("ERR:NO_GSM_NETWORK");
        return;
    }

    // GPRS connect
    if (modem.gprsConnect(SIM_APN, SIM_APN_USER, SIM_APN_PASS)) {
        gprsConnected = true;
        sendCommandToSTM32("INFO:GPRS_OK");
    } else {
        gprsConnected = false;
        sendCommandToSTM32("ERR:GPRS_FAIL");
        return;
    }

    connectMQTT();

    if (mqttClient.connected()) {
        sendCommandToSTM32("INFO:MQTT_OK");
    } else {
        sendCommandToSTM32("ERR:MQTT_FAIL");
    }
}

// ============================================================
//  maintainGPRS()
// ============================================================
void maintainGPRS() {
    if (!modem.isGprsConnected()) {
        gprsConnected = false;
        sendCommandToSTM32("ERR:GPRS_LOST");

        if (modem.gprsConnect(SIM_APN, SIM_APN_USER, SIM_APN_PASS)) {
            gprsConnected = true;
            sendCommandToSTM32("INFO:GPRS_RECONNECTED");
        } else {
            sendCommandToSTM32("ERR:GPRS_RECONNECT_FAIL");
        }
    }
}

// ============================================================
//  loopMQTT()  — call every loop()
// ============================================================
void loopMQTT() {
    if (!gprsConnected) return;

    if (!mqttClient.connected()) {
        unsigned long now = millis();
        if (now - lastMqttReconnectAttempt > MQTT_RECONNECT_INTERVAL) {
            lastMqttReconnectAttempt = now;
            sendCommandToSTM32("ERR:MQTT_DISCONNECTED");
            connectMQTT();
        }
        return;
    }

    mqttClient.loop();
}

// ============================================================
//  publishSensorMQTT()
//  Receives sensor data forwarded from STM32 via SoftwareSerial,
//  then publishes it to the MQTT broker.
// ============================================================
void publishSensorMQTT(float temp, float hum, uint16_t co2,
                       float lux, float pressure, int gas, int soil) {
    if (!mqttClient.connected()) return;

    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"temp\":%.1f,\"hum\":%.1f,\"co2\":%u,\"lux\":%.1f,"
        "\"pressure\":%.1f,\"gas\":%d,\"soil\":%d}",
        temp, hum, co2, lux, pressure, gas, soil
    );

    mqttClient.publish(MQTT_TOPIC_SENSORS, payload);
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
//  Forward unsolicited modem URCs to STM32 for logging.
//  Call every loop().
// ============================================================
void updateSIM_Connection() {
    if (SerialSIM.available()) {
        String incoming = SerialSIM.readStringUntil('\n');
        incoming.trim();
        if (incoming.length() > 0) {
            // Relay URC to STM32 as a prefixed info string
            String fwd = "URC:" + incoming;
            sendCommandToSTM32(fwd);
        }
    }
}

// ============================================================
//  sendSMS_Alert()
// ============================================================
void sendSMS_Alert(const String& phoneNumber, const String& message) {
    modem.sendAT(GF("+CMGF=1"));
    if (modem.waitResponse(1000) != 1) {
        sendCommandToSTM32("ERR:SMS_TEXTMODE_FAIL");
        return;
    }

    SerialSIM.print("AT+CMGS=\"");
    SerialSIM.print(phoneNumber);
    SerialSIM.print("\"\r");

    unsigned long start = millis();
    bool gotPrompt = false;
    while (millis() - start < 5000) {
        if (SerialSIM.available() && SerialSIM.read() == '>') {
            gotPrompt = true;
            break;
        }
    }

    if (!gotPrompt) {
        SerialSIM.write(27); // ESC
        delay(200);
        clearSIMBuffer();
        sendCommandToSTM32("ERR:SMS_NO_PROMPT");
        return;
    }

    SerialSIM.print(message);
    delay(100);
    SerialSIM.write(26); // Ctrl+Z

    unsigned long waitStart = millis();
    String response = "";
    while (millis() - waitStart < SMS_SEND_TIMEOUT_MS) {
        if (SerialSIM.available()) {
            char c = SerialSIM.read();
            response += c;
            if (response.indexOf("+CMGS") >= 0 || response.indexOf("OK") >= 0) {
                sendCommandToSTM32("INFO:SMS_SENT_OK");
                return;
            }
            if (response.indexOf("ERROR") >= 0) {
                sendCommandToSTM32("ERR:SMS_ERROR");
                return;
            }
        }
    }
    sendCommandToSTM32("ERR:SMS_TIMEOUT");
}