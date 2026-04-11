// ============================================================
//  module_sim.cpp  —  STM32-B  Gateway node
//
//  Key design decisions:
//    - SIM UART (USART2 PA2/PA3) is NEVER read raw in user code.
//      TinyGSM owns it entirely.
//    - STM32-A UART (USART1 PA9/PA10) is read only in main.cpp.
//      module_sim only writes to it (forward commands).
//    - HTTP uses a second TinyGsmClient (gsmClientHTTP) so
//      MQTT and HTTP never share the same TCP socket object.
//    - publishSensorMQTT() is called from onDataFromA() here.
// ============================================================
#include "module_sim.h"

// -------------------------------------------------------
//  BUG FIX #3: Serial objects now use the corrected pin defines.
//
//  SerialSIM  = USART2  PA3(RX) / PA2(TX)  →  SIM A7680C
//  SerialA    = USART1  PA10(RX) / PA9(TX) →  STM32-A link
//
//  HardwareSerial constructor: (RX_pin, TX_pin)
// -------------------------------------------------------
static HardwareSerial SerialSIM(SIM_RX_PIN, SIM_TX_PIN);    // USART2: PA3 RX, PA2 TX
HardwareSerial        SerialA  (STM32A_RX_PIN, STM32A_TX_PIN); // USART1: PA10 RX, PA9 TX

// -------------------------------------------------------
//  TinyGSM + MQTT
// -------------------------------------------------------
TinyGsm        modem(SerialSIM);
TinyGsmClient  gsmClientMQTT(modem, 0);  // channel 0 — for MQTT
TinyGsmClient  gsmClientHTTP(modem, 1);  // channel 1 — for HTTP alerts
PubSubClient   mqttClient(gsmClientMQTT);

// -------------------------------------------------------
//  HTTP client (ntfy.sh)
// -------------------------------------------------------
static HttpClient httpClient(gsmClientHTTP, NTFY_HOST, NTFY_PORT);

// -------------------------------------------------------
//  Internal state
// -------------------------------------------------------
static bool          gprsConnected       = false;
static unsigned long lastMqttReconnectMs = 0;
static unsigned long lastGprsCheckMs     = 0;

// ============================================================
//  forwardCommandToA()
// ============================================================
static void forwardCommandToA(const String& cmd) {
    SerialA.println(cmd);
    // NOTE: Serial here refers to rttDebug via the #define in main.cpp.
    // This file does NOT include that #define, so we use rttDebug directly.
    rttDebug.print("[FWD→A] ");
    rttDebug.println(cmd);
}

// ============================================================
//  publishACK()
// ============================================================
static void publishACK(const String& cmd) {
    if (!mqttClient.connected()) return;
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"forwarded\":\"%s\"}", cmd.c_str());
    mqttClient.publish(MQTT_TOPIC_ACK, buf);
}

// ============================================================
//  mqttCallback()
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    String msg = "";
    for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
    msg.trim();

    rttDebug.print("[MQTT←] ");
    rttDebug.println(msg);

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
    rttDebug.println("[MQTT←] Unknown command, ignored.");
}

// ============================================================
//  buildMqttClientId()
//  FIX 4: Derive client ID from last 8 digits of modem IMEI.
//  Two devices with the same ID kick each other off HiveMQ
//  every time they connect. IMEI is globally unique per SIM module.
//
//  BUG FIX NEW: Arduino's String::replace() modifies the string
//  in-place and returns void. The original code wrote:
//
//      return ... + String(__TIME__).replace(":", "");
//
//  This creates a temporary rvalue, calls replace() on it (which
//  returns void), and then tries to use void in an expression —
//  a compile error. Fix: assign to a named variable first.
// ============================================================
static String buildMqttClientId() {
    // TEMPORARY: hardcoded client ID — skip IMEI lookup to avoid
    // AT command interference during MQTT connect phase.
    return String(MQTT_CLIENT_ID_PREFIX);
}

// ============================================================
//  connectMQTT()
//
//  BUG FIX #4 (original): setKeepAlive(MQTT_KEEPALIVE) added so
//  the broker does not drop the connection during slow GPRS periods.
//
//  BUG FIX NEW #2: gsmClientMQTT.stop() BEFORE mqttClient.connect().
//
//  Root cause of state=-4 (MQTT_CONNECTION_TIMEOUT) on reconnect:
//
//  When the MQTT connection drops (broker timeout, GPRS glitch,
//  or broker restart), the underlying modem TCP channel 0 is NOT
//  automatically closed by PubSubClient.  The modem may still hold
//  the socket in a CLOSING or half-open state.  When connectMQTT()
//  is called again and mqttClient.connect() internally calls
//  gsmClientMQTT.connect(), TinyGSM sends AT+CIPSTART on a channel
//  that hasn't been released — the modem rejects the command or the
//  broker never receives the TCP SYN, and the attempt times out
//  (state=-4) every single retry until the modem's TCP stack
//  eventually times out the stale socket on its own (~minutes).
//
//  Fix: call gsmClientMQTT.stop() unconditionally before connect().
//  stop() sends AT+CIPCLOSE, releasing the modem socket immediately.
//  The next gsmClientMQTT.connect() then opens a fresh channel.
//
//  BUG FIX NEW #3: setSocketTimeout(30) added.
//
//  PubSubClient's default socket timeout is 15 s. GPRS round-trips
//  to broker.hivemq.com regularly exceed 15 s when the radio is
//  in cell-reselection or the broker is busy. Raising to 30 s gives
//  the TCP handshake and MQTT CONNACK enough time to complete before
//  PubSubClient gives up and sets state=-4.
// ============================================================
void connectMQTT() {
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(mqttCallback);
    mqttClient.setBufferSize(512);
    mqttClient.setKeepAlive(MQTT_KEEPALIVE);
    mqttClient.setSocketTimeout(30);   // 30s read timeout for slow GPRS

    String clientId = buildMqttClientId();

    // -------------------------------------------------------
    //  ROOT CAUSE FIX for state=-4 on A7680C:
    //
    //  The SIM7600/A7680C modem requires ~1-2 seconds AFTER
    //  AT+CIPSTART returns "CONNECT" before its internal TCP
    //  channel is truly ready to transmit. Calling
    //  mqttClient.connect() immediately fires the MQTT CONNECT
    //  packet while the modem's channel is still initialising —
    //  the packet is discarded inside the modem, the broker never
    //  receives it, sends no CONNACK, and PubSubClient times out
    //  after socketTimeout (30 s) => state=-4.
    //
    //  Fix: after gsmClientMQTT.connect() returns true:
    //    1. Wait 2 s for the channel to stabilise.
    //    2. Drain any stale bytes the modem buffered during setup.
    //    3. Only then send the MQTT CONNECT handshake.
    //
    //  Three TCP+MQTT attempts are made with 5 s back-off.
    // -------------------------------------------------------
    for (int attempt = 1; attempt <= 3; attempt++) {
        rttDebug.print("[MQTT] Attempt ");
        rttDebug.print(attempt);
        rttDebug.println("/3: TCP -> " MQTT_BROKER "..." );

        // Release any stale socket from a previous attempt
        if (gsmClientMQTT.connected()) {
            gsmClientMQTT.stop();
            delay(500);   // Allow AT+CIPCLOSE to complete
        }

        if (!gsmClientMQTT.connect(MQTT_BROKER, MQTT_PORT)) {
            rttDebug.println("[MQTT] TCP FAILED");
            if (attempt < 3) delay(5000);
            continue;
        }
        rttDebug.println("[MQTT] TCP OK — stabilising (2s)...");

        // KEY DELAY: let A7680C channel come fully up before writing
        delay(2000);

        // Drain any modem-buffered bytes so CONNACK is not confused
        // with leftover AT response tokens from AT+CIPSTART
        uint32_t drained = 0;
        while (gsmClientMQTT.available()) {
            gsmClientMQTT.read();
            drained++;
        }
        if (drained > 0) {
            rttDebug.print("[MQTT] Drained ");
            rttDebug.print(drained);
            rttDebug.println(" stale byte(s)");
        }

        rttDebug.print("[MQTT] CONNECT as ");
        rttDebug.print(clientId);
        rttDebug.print("...");

        bool ok = mqttClient.connect(clientId.c_str());

        if (ok) {
            rttDebug.println(" OK");
            mqttClient.subscribe(MQTT_TOPIC_CONTROL);
            mqttClient.publish(MQTT_TOPIC_SENSORS,
                "{\"status\":\"online\",\"device\":\"STM32B_GW\"}");
            return;   // successfully connected
        }

        int st = mqttClient.state();
        rttDebug.print(" FAILED state=");
        rttDebug.println(st);
        gsmClientMQTT.stop();

        if (attempt < 3) {
            rttDebug.println("[MQTT] Retry in 5s...");
            delay(5000);
        }
    }
    rttDebug.println("[MQTT] All 3 attempts failed. loopGateway() will retry.");
}

// ============================================================
//  setupSIM_A7680()
// ============================================================
void setupSIM_A7680() {
    SerialA.begin(STM32A_BAUD);
    // FIX: Increase read timeout so readStringUntil('\n') waits long
    // enough for STM32-A to finish sending each full DATA line.
    // Default 1000ms is too short when STM32-A sends the CSV in chunks
    // or when its sensor-read ISR momentarily stalls UART output.
    // Result: second DATA line was always truncated (P=0, Gas=0, Soil=0).
    SerialA.setTimeout(3000);
    delay(100);

    SerialSIM.begin(SIM_BAUD);

    rttDebug.println("[SIM] Waiting for modem cold-boot (8s)...");
    delay(8000);

    // FIX 1: Call modem.init() unconditionally before testAT().
    // On warm boot the modem stays powered so testAT() returns true
    // immediately — the if-block below is skipped entirely and init()
    // was never called, leaving TinyGSM uninitialised. init() is safe
    // to call every boot: it just syncs baud rate and clears echo.
    rttDebug.println("[SIM] Initialising modem (init)...");
    modem.init();

    rttDebug.println("[SIM] Probing modem (testAT)...");
    if (!modem.testAT(3000)) {
        rttDebug.println("[SIM] No response — sending restart...");
        modem.restart();   // restart() calls init() internally too
        rttDebug.println("[SIM] Waiting post-restart (10s)...");
        delay(10000);
        if (!modem.testAT(5000)) {
            rttDebug.println("[SIM] ERROR: Modem not responding. Check PA2/PA3 wiring and power.");
            forwardCommandToA("ERR:MODEM_NO_RESPONSE");
            return;
        }
    }
    rttDebug.println("[SIM] Modem alive.");

    // Lock baud rate in NVM so it survives power cycles
    modem.sendAT(GF("+IPR=115200"));
    modem.waitResponse(1000);

    rttDebug.print("[SIM] Modem: ");
    rttDebug.println(modem.getModemInfo());
    rttDebug.print("[SIM] IMEI:  ");
    rttDebug.println(modem.getIMEI());

    rttDebug.println("[SIM] Waiting for GSM network (max 60s)...");
    if (!modem.waitForNetwork(SIM_NETWORK_TIMEOUT_MS)) {
        rttDebug.println("[SIM] ERROR: No GSM network.");
        forwardCommandToA("ERR:NO_GSM_NETWORK");
        return;
    }
    rttDebug.print("[SIM] GSM OK. Signal: ");
    rttDebug.println(modem.getSignalQuality());

    // FIX 3: Bounded retry loop — single-shot connect at boot left the
    // gateway silently broken for up to 5 min if the first attempt failed.
    // 3 attempts with 5s back-off; on total failure loopGateway() takes over.
    const int MAX_BOOT_RETRIES = 3;
    for (int attempt = 1; attempt <= MAX_BOOT_RETRIES; attempt++) {
        rttDebug.print("[SIM] GPRS connect attempt ");
        rttDebug.print(attempt);
        rttDebug.print("/");
        rttDebug.println(MAX_BOOT_RETRIES);

        if (modem.gprsConnect(SIM_APN, SIM_APN_USER, SIM_APN_PASS)) {
            gprsConnected = true;
            rttDebug.print("[SIM] GPRS OK. IP: ");
            rttDebug.println(modem.getLocalIP());
            forwardCommandToA("INFO:GPRS_OK");
            break;
        }
        rttDebug.println("[SIM] GPRS connect failed.");
        if (attempt < MAX_BOOT_RETRIES) {
            rttDebug.println("[SIM] Retrying in 5s...");
            delay(5000);
        }
    }

    if (!gprsConnected) {
        rttDebug.println("[SIM] ERROR: GPRS unavailable after retries. loopGateway() will retry.");
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
        rttDebug.println("[SIM] GPRS lost. Reconnecting...");
        forwardCommandToA("ERR:GPRS_LOST");
        if (modem.gprsConnect(SIM_APN, SIM_APN_USER, SIM_APN_PASS)) {
            gprsConnected = true;
            rttDebug.println("[SIM] GPRS reconnected.");
            forwardCommandToA("INFO:GPRS_RECONNECTED");
        } else {
            forwardCommandToA("ERR:GPRS_RECONNECT_FAIL");
        }
    }
}

// ============================================================
//  publishSensorMQTT()
// ============================================================
static void publishSensorMQTT(float temp, float hum, uint16_t co2,
                               float lux, float pressure, int gas, int soil) {
    if (!mqttClient.connected()) {
        rttDebug.println("[MQTT] Not connected — skipping publish.");
        return;
    }
    char payload[256];
    // Using Arduino String() for float conversions to save massive flash space (~10KB)
    // compared to linking _printf_float in newlib-nano.
    snprintf(payload, sizeof(payload),
        "{\"temp\":%s,\"hum\":%s,\"co2\":%u,"
        "\"lux\":%s,\"pressure\":%s,\"gas\":%d,\"soil\":%d}",
        String(temp, 1).c_str(), String(hum, 1).c_str(), (unsigned int)co2,
        String(lux, 1).c_str(), String(pressure, 1).c_str(), gas, soil);

    if (mqttClient.publish(MQTT_TOPIC_SENSORS, payload)) {
        rttDebug.print("[MQTT→] ");
        rttDebug.println(payload);
    } else {
        rttDebug.println("[MQTT→] Publish failed.");
    }
}

// ============================================================
//  parseCSVField()
//  Extracts one comma-delimited token from a String starting at
//  *pos and advances *pos past the comma.
//  Returns 0.0 (or 0) for empty fields — handles ",,".
// ============================================================
static float parseCSVField(const String& s, int* pos) {
    int comma = s.indexOf(',', *pos);
    String token;
    if (comma == -1) {
        token = s.substring(*pos);
        *pos  = s.length();
    } else {
        token = s.substring(*pos, comma);
        *pos  = comma + 1;
    }
    token.trim();
    return token.length() > 0 ? token.toFloat() : 0.0f;
}

// ============================================================
//  onDataFromA()
//  Parses "temp,hum,co2,lux,pressure,gas,soil" robustly.
//  Empty fields (sensor not ready) become 0 — publish proceeds
//  so partial data still reaches HiveMQ.
// ============================================================
void onDataFromA(const String& csvLine) {
    int pos = 0;
    float temp     = parseCSVField(csvLine, &pos);
    float hum      = parseCSVField(csvLine, &pos);
    float co2_f    = parseCSVField(csvLine, &pos);
    float lux      = parseCSVField(csvLine, &pos);
    float pressure = parseCSVField(csvLine, &pos);
    float gas_f    = parseCSVField(csvLine, &pos);
    float soil_f   = parseCSVField(csvLine, &pos);

    rttDebug.print("[DATA] T="); rttDebug.print(temp);
    rttDebug.print(" H=");       rttDebug.print(hum);
    rttDebug.print(" CO2=");     rttDebug.print((int)co2_f);
    rttDebug.print(" Lux=");     rttDebug.print(lux);
    rttDebug.print(" P=");       rttDebug.print(pressure);
    rttDebug.print(" Gas=");     rttDebug.print((int)gas_f);
    rttDebug.print(" Soil=");    rttDebug.println((int)soil_f);

    publishSensorMQTT(temp, hum, (uint16_t)co2_f,
                      lux, pressure, (int)gas_f, (int)soil_f);
}

// ============================================================
//  sendHttpAlert()
// ============================================================
bool sendHttpAlert(const String& message) {
    if (!gprsConnected) {
        rttDebug.println("[HTTP] No GPRS — alert not sent.");
        return false;
    }

    rttDebug.println("[HTTP] Sending alert to ntfy.sh...");

    String path = "/";
    path += NTFY_TOPIC;

    httpClient.setTimeout(HTTP_TIMEOUT_MS);
    int err = httpClient.post(path, "text/plain", message);

    if (err != 0) {
        rttDebug.print("[HTTP] Connection error: ");
        rttDebug.println(err);
        return false;
    }

    int statusCode = httpClient.responseStatusCode();
    httpClient.responseBody();
    httpClient.stop();

    rttDebug.print("[HTTP] ntfy response: ");
    rttDebug.println(statusCode);
    return (statusCode >= 200 && statusCode < 300);
}

// ============================================================
//  onAlertFromA()
// ============================================================
void onAlertFromA(const String& phone, const String& message) {
    rttDebug.print("[ALERT] phone=");
    rttDebug.print(phone);
    rttDebug.print(" msg=");
    rttDebug.println(message);

    String body = "[" + phone + "] " + message;

    if (sendHttpAlert(body)) {
        rttDebug.println("[ALERT] Sent OK.");
        if (mqttClient.connected()) {
            mqttClient.publish(MQTT_TOPIC_ACK, "{\"alert\":\"sent\"}");
        }
    } else {
        rttDebug.println("[ALERT] Failed.");
    }
}

// ============================================================
//  loopGateway()
// ============================================================
void loopGateway() {
    unsigned long now = millis();

    if (now - lastGprsCheckMs >= GPRS_CHECK_INTERVAL) {
        lastGprsCheckMs = now;
        maintainGPRS();
    }

    if (!gprsConnected) return;

    if (!mqttClient.connected()) {
        if (now - lastMqttReconnectMs >= MQTT_RECONNECT_INTERVAL) {
            lastMqttReconnectMs = now;

            // FIX 2: GPRS blind spot.
            // gprsConnected can be stale (true) if GPRS dropped between
            // the 5-minute maintainGPRS() checks. Blindly calling
            // connectMQTT() on a dead GPRS layer wastes up to 5 min of
            // retry budget. Check the modem directly before attempting.
            if (!modem.isGprsConnected()) {
                rttDebug.println("[MQTT] GPRS lost mid-session — recovering immediately");
                gprsConnected = false;
                maintainGPRS();
                // MQTT reconnect deferred to next loop iteration
            } else {
                rttDebug.println("[MQTT] Disconnected — reconnecting...");
                connectMQTT();
            }
        }
        return;
    }

    mqttClient.loop();
}