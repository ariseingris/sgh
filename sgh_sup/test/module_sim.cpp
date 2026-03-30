// ============================================================
//  module_sim.cpp  —  STM32-B  Gateway node
//
//  Key design decisions:
//    - SIM UART (USART1 PA9/PA10) is NEVER read raw in user code.
//      TinyGSM owns it entirely. updateSIM_Connection() is
//      removed — it was starving TinyGSM's buffer.
//    - STM32-A UART (USART2) is read only in main.cpp loop.
//      module_sim only writes to it (forward commands).
//    - HTTP uses a second TinyGsmClient (gsmClientHTTP) so
//      MQTT and HTTP never share the same TCP socket object.
//    - publishSensorMQTT() is called from onDataFromA() here,
//      not left dangling as in the old code.
// ============================================================

#include "module_sim.h"

// -------------------------------------------------------
//  USART1  ->  SIM A7680C  (PA10 RX, PA9 TX)
//  Uses defines from module_sim.h so wiring is one source of truth.
// -------------------------------------------------------
static HardwareSerial SerialSIM(SIM_RX_PIN, SIM_TX_PIN);   // PA10, PA9

// -------------------------------------------------------
//  USART2  ->  STM32-A  (PA3 RX, PA2 TX)
//  Per hardware diagram: B-PA2→A-PA9(RX), B-PA3←A-PA10(TX)
//  Exposed via extern so main.cpp can read it directly.
// -------------------------------------------------------
HardwareSerial SerialA(STM32A_RX_PIN, STM32A_TX_PIN);      // PA3, PA2
// -------------------------------------------------------
//  TinyGSM + MQTT
// -------------------------------------------------------
TinyGsm        modem(SerialSIM);
TinyGsmClient  gsmClientMQTT(modem, 0);  // channel 0 — for MQTT
TinyGsmClient  gsmClientHTTP(modem, 1);   // channel 1 — for HTTP alerts
PubSubClient   mqttClient(gsmClientMQTT);

// -------------------------------------------------------
//  HTTP client (ntfy.sh)
// -------------------------------------------------------
static HttpClient httpClient(gsmClientHTTP, NTFY_HOST, NTFY_PORT);

// -------------------------------------------------------
//  Internal state
// -------------------------------------------------------
static bool          gprsConnected          = false;
static unsigned long lastMqttReconnectMs    = 0;
static unsigned long lastGprsCheckMs        = 0;

// ============================================================
//  forwardCommandToA()
//  Sends a newline-terminated command string to STM32-A.
// ============================================================
static void forwardCommandToA(const String& cmd) {
    SerialA.println(cmd);
    Serial.print("[FWD→A] ");
    Serial.println(cmd);
}

// ============================================================
//  publishACK()
//  Sends ACK back to MQTT broker after forwarding a command.
// ============================================================
static void publishACK(const String& cmd) {
    if (!mqttClient.connected()) return;
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"forwarded\":\"%s\"}", cmd.c_str());
    mqttClient.publish(MQTT_TOPIC_ACK, buf);
}

// ============================================================
//  mqttCallback()
//  Receives commands from broker → forwards to STM32-A.
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    Serial.print("[MQTT←] ");
    Serial.println(msg);

    const char* validCmds[] = {
        "SYSTEM_ON", "SYSTEM_OFF",
        "FAN_ON",    "FAN_OFF",
        "PISTON_OPEN", "PISTON_CLOSE"
    };
    for (auto& c : validCmds) {
        if (msg == c) {
            forwardCommandToA(msg);
            publishACK(msg);
            return;
        }
    }
    Serial.println("[MQTT←] Unknown command, ignored.");
}

// ============================================================
//  connectMQTT()
// ============================================================
void connectMQTT() {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);

    Serial.print("[MQTT] Connecting to ");
    Serial.print(MQTT_BROKER);
    Serial.print("...");

    if (mqttClient.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS)) {
        Serial.println(" OK");
        mqttClient.subscribe(MQTT_TOPIC_CONTROL);
        mqttClient.publish(MQTT_TOPIC_SENSORS,
            "{\"status\":\"online\",\"device\":\"STM32B_GW\"}");
    } else {
        Serial.print(" FAILED, state=");
        Serial.println(mqttClient.state());
    }
}

// ============================================================
//  setupSIM_A7680()
// ============================================================
void setupSIM_A7680() {
    SerialA.begin(STM32A_BAUD);
    delay(100);

    SerialSIM.begin(SIM_BAUD);

    // -------------------------------------------------------
    //  Cold-boot wait: A7680C emits "RDY" ~3-5s after power-on.
    //  Give it 8s before touching it so the UART is stable.
    // -------------------------------------------------------
    Serial.println("[SIM] Waiting for modem cold-boot (8s)...");
    delay(8000);

    // -------------------------------------------------------
    //  Probe before restart. If modem is already alive, skip
    //  the hard restart (restart() causes another ~10s dead window).
    // -------------------------------------------------------
    Serial.println("[SIM] Probing modem (testAT)...");
    if (!modem.testAT(3000)) {
        Serial.println("[SIM] No response — sending restart...");
        modem.restart();
        Serial.println("[SIM] Waiting post-restart (10s)...");
        delay(10000);
        if (!modem.testAT(5000)) {
            Serial.println("[SIM] ERROR: Modem not responding. Check PA9/PA10 wiring and power.");
            forwardCommandToA("ERR:MODEM_NO_RESPONSE");
            return;
        }
    }
    Serial.println("[SIM] Modem alive.");

    // Lock baud rate in NVM so it survives power cycles
    modem.sendAT(GF("+IPR=115200"));
    modem.waitResponse(1000);

    Serial.print("[SIM] Modem: ");
    Serial.println(modem.getModemInfo());
    Serial.print("[SIM] IMEI:  ");
    Serial.println(modem.getIMEI());

    Serial.println("[SIM] Waiting for GSM network (max 60s)...");
    if (!modem.waitForNetwork(SIM_NETWORK_TIMEOUT_MS)) {
        Serial.println("[SIM] ERROR: No GSM network.");
        forwardCommandToA("ERR:NO_GSM_NETWORK");
        return;
    }
    Serial.print("[SIM] GSM OK. Signal: ");
    Serial.println(modem.getSignalQuality());

    Serial.print("[SIM] Connecting GPRS (APN=");
    Serial.print(SIM_APN);
    Serial.println(")...");
    if (modem.gprsConnect(SIM_APN, SIM_APN_USER, SIM_APN_PASS)) {
        gprsConnected = true;
        Serial.print("[SIM] GPRS OK. IP: ");
        Serial.println(modem.getLocalIP());
        forwardCommandToA("INFO:GPRS_OK");
    } else {
        Serial.println("[SIM] ERROR: GPRS failed.");
        forwardCommandToA("ERR:GPRS_FAIL");
        return;
    }

    connectMQTT();
    forwardCommandToA(mqttClient.connected() ? "INFO:MQTT_OK" : "ERR:MQTT_FAIL");
}

// ============================================================
//  maintainGPRS()
// ============================================================
void maintainGPRS() {
    if (!modem.isGprsConnected()) {
        gprsConnected = false;
        Serial.println("[SIM] GPRS lost. Reconnecting...");
        forwardCommandToA("ERR:GPRS_LOST");
        if (modem.gprsConnect(SIM_APN, SIM_APN_USER, SIM_APN_PASS)) {
            gprsConnected = true;
            Serial.println("[SIM] GPRS reconnected.");
            forwardCommandToA("INFO:GPRS_RECONNECTED");
        } else {
            forwardCommandToA("ERR:GPRS_RECONNECT_FAIL");
        }
    }
}

// ============================================================
//  publishSensorMQTT()
//  Builds JSON and publishes to MQTT_TOPIC_SENSORS.
// ============================================================
static void publishSensorMQTT(float temp, float hum, uint16_t co2,
                               float lux, float pressure, int gas, int soil) {
    if (!mqttClient.connected()) {
        Serial.println("[MQTT] Not connected — skipping publish.");
        return;
    }
    char payload[256];
    snprintf(payload, sizeof(payload),
        "{\"temp\":%.1f,\"hum\":%.1f,\"co2\":%u,"
        "\"lux\":%.1f,\"pressure\":%.1f,\"gas\":%d,\"soil\":%d}",
        temp, hum, co2, lux, pressure, gas, soil);

    if (mqttClient.publish(MQTT_TOPIC_SENSORS, payload)) {
        Serial.print("[MQTT→] ");
        Serial.println(payload);
    } else {
        Serial.println("[MQTT→] Publish failed.");
    }
}

// ============================================================
//  onDataFromA()
//  Parse "DATA:<temp>,<hum>,<co2>,<lux>,<pressure>,<gas>,<soil>"
//  then publish to MQTT.
// ============================================================
void onDataFromA(const String& csvLine) {
    // csvLine = "28.3,65.0,412,1200.0,1013.2,320,540"
    float    temp = 0, hum = 0, lux = 0, pressure = 0;
    uint16_t co2  = 0;
    int      gas  = 0, soil = 0;

    int n = sscanf(csvLine.c_str(),
                   "%f,%f,%hu,%f,%f,%d,%d",
                   &temp, &hum, &co2, &lux, &pressure, &gas, &soil);

    if (n == 7) {
        publishSensorMQTT(temp, hum, co2, lux, pressure, gas, soil);
    } else {
        Serial.print("[DATA] Parse error, fields=");
        Serial.println(n);
    }
}

// ============================================================
//  sendHttpAlert()
//  HTTP POST to ntfy.sh.
//  Returns true on success.
//  Uses gsmClientHTTP (channel 1) — independent of MQTT.
// ============================================================
bool sendHttpAlert(const String& message) {
    if (!gprsConnected) {
        Serial.println("[HTTP] No GPRS — alert not sent.");
        return false;
    }

    Serial.println("[HTTP] Sending alert to ntfy.sh...");

    String path = "/";
    path += NTFY_TOPIC;

    httpClient.setTimeout(HTTP_TIMEOUT_MS);
    int err = httpClient.post(path, "text/plain", message);

    if (err != 0) {
        Serial.print("[HTTP] Connection error: ");
        Serial.println(err);
        return false;
    }

    int statusCode = httpClient.responseStatusCode();
    httpClient.responseBody();   // drain the body
    httpClient.stop();

    Serial.print("[HTTP] ntfy response: ");
    Serial.println(statusCode);
    return (statusCode >= 200 && statusCode < 300);
}

// ============================================================
//  onAlertFromA()
//  Parse "ALERT:<phone>,<message>" and POST to ntfy.sh.
//  The phone field is included in the ntfy title header
//  (ntfy doesn't actually send SMS — it pushes to the app).
// ============================================================
void onAlertFromA(const String& phone, const String& message) {
    Serial.print("[ALERT] phone=");
    Serial.print(phone);
    Serial.print(" msg=");
    Serial.println(message);

    // ntfy supports a Title header but ArduinoHttpClient's simple
    // post() doesn't set custom headers easily.  We include the
    // phone in the body for now; use beginRequest/endRequest for headers.
    String body = "[" + phone + "] " + message;

    if (sendHttpAlert(body)) {
        Serial.println("[ALERT] Sent OK.");
        if (mqttClient.connected()) {
            mqttClient.publish(MQTT_TOPIC_ACK, "{\"alert\":\"sent\"}");
        }
    } else {
        Serial.println("[ALERT] Failed.");
    }
}

// ============================================================
//  loopGateway()
//  Call every loop().
// ============================================================
void loopGateway() {
    unsigned long now = millis();

    // GPRS watchdog
    if (now - lastGprsCheckMs >= GPRS_CHECK_INTERVAL) {
        lastGprsCheckMs = now;
        maintainGPRS();
    }

    if (!gprsConnected) return;

    // MQTT reconnect
    if (!mqttClient.connected()) {
        if (now - lastMqttReconnectMs >= MQTT_RECONNECT_INTERVAL) {
            lastMqttReconnectMs = now;
            Serial.println("[MQTT] Disconnected — reconnecting...");
            connectMQTT();
        }
        return;
    }

    mqttClient.loop();   // keepalive + incoming message dispatch
}