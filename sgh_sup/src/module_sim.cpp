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
// NOTE: T3 (TLS via TinyGsmClientSecure) NOT applied — TinyGSM 0.11.7's
// SIM7600 secure client is commented out (TODO). Needs library upgrade or
// native SIMCOM AT-level MQTT (AT+CMQTTSSLCFG). See chat for options.
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

// -------------------------------------------------------
//  FLAW-4 FIX: Wrapper API so main.cpp never touches mqttClient directly.
//  BUG-C1 NOTE: PubSubClient only supports QoS 0 for publish.
//  For QoS 1 publish, a different library would be needed.
// -------------------------------------------------------
bool isMqttConnected() {
    return mqttClient.connected();
}

bool mqttPublish(const char* topic, const char* payload, bool retained) {
    if (!mqttClient.connected()) return false;
    return mqttClient.publish(topic, payload, retained);
}

// -------------------------------------------------------
//  FLAW-6 FIX: Unified deferred ACK queue.
//  Supports multiple pending ACKs (2 slots: command ACK + STM32-A ACK).
// -------------------------------------------------------
#define ACK_QUEUE_SIZE 4
#define ACK_PAYLOAD_LEN 80
static char  ackQueue[ACK_QUEUE_SIZE][ACK_PAYLOAD_LEN];
static int   ackQueueHead  = 0;
static int   ackQueueTail  = 0;
static int   ackQueueCount = 0;

void queueAckPublish(const char* payload) {
    if (ackQueueCount >= ACK_QUEUE_SIZE) return;  // drop if full
    strncpy(ackQueue[ackQueueHead], payload, ACK_PAYLOAD_LEN - 1);
    ackQueue[ackQueueHead][ACK_PAYLOAD_LEN - 1] = '\0';
    ackQueueHead = (ackQueueHead + 1) % ACK_QUEUE_SIZE;
    ackQueueCount++;
}

void processDeferredAck() {
    if (ackQueueCount == 0) return;
    if (!mqttClient.connected()) {
        // Clear queue if disconnected
        ackQueueCount = 0;
        ackQueueHead = ackQueueTail = 0;
        return;
    }
    // Publish one per loop() to avoid stacking GPRS I/O
    mqttClient.publish(mqttTopicAck, ackQueue[ackQueueTail]);
    ackQueueTail = (ackQueueTail + 1) % ACK_QUEUE_SIZE;
    ackQueueCount--;
}

// Per-device MQTT topics — built from IMEI at setupSIM_A7680() time
char mqttTopicSensors[64];
char mqttTopicControl[64];
char mqttTopicAck[64];
char mqttTopicHeartbeat[64];   // FLAW-5 FIX

// -------------------------------------------------------
//  Ring buffer — stores payloads while MQTT is unavailable
// -------------------------------------------------------
static char  sensorBuffer[SENSOR_BUFFER_SLOTS][SENSOR_PAYLOAD_LEN];
static int   bufHead = 0;
static int   bufTail = 0;
static int   bufCount = 0;

static void bufferPush(const char* payload) {
    strncpy(sensorBuffer[bufHead], payload, SENSOR_PAYLOAD_LEN - 1);
    sensorBuffer[bufHead][SENSOR_PAYLOAD_LEN - 1] = '\0';
    bufHead = (bufHead + 1) % SENSOR_BUFFER_SLOTS;
    if (bufCount < SENSOR_BUFFER_SLOTS) bufCount++;
    else bufTail = (bufTail + 1) % SENSOR_BUFFER_SLOTS; // overwrite oldest
}

static bool bufferPop(char* out) {
    if (bufCount == 0) return false;
    strncpy(out, sensorBuffer[bufTail], SENSOR_PAYLOAD_LEN);
    bufTail = (bufTail + 1) % SENSOR_BUFFER_SLOTS;
    bufCount--;
    return true;
}

// BUG-C7 FIX: Don't re-push failed entries — that overwrites oldest
// entry in a full ring buffer causing silent data loss.
// Instead, stop flushing and let the entry be lost (logged).
static void flushBuffer() {
    char entry[SENSOR_PAYLOAD_LEN];
    int flushed = 0;
    while (flushed < 2 && bufferPop(entry)) {
        if (!mqttClient.publish(mqttTopicSensors, entry)) {
            rttDebug.println("[MQTT] Buffer flush failed — entry lost.");
            break;
        }
        mqttClient.loop();
        flushed++;
    }
}

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
//  mqttCallback()
//
//  LATENCY FIX L4: Use raw payload buffer directly instead of
//  char-by-char String concatenation.
//
//  LATENCY FIX L5: Replaced synchronous publishACK() with
//  deferred queue. ACK is published in next loop() iteration
//  via processDeferredAck(), avoiding blocking GPRS I/O
//  inside the PubSubClient callback context.
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
    // L4 FIX: copy payload to null-terminated buffer directly
    char msg[32];
    unsigned int copyLen = (length < sizeof(msg) - 1) ? length : sizeof(msg) - 1;
    memcpy(msg, payload, copyLen);
    msg[copyLen] = '\0';
    // Trim trailing whitespace
    while (copyLen > 0 && (msg[copyLen-1] == ' ' || msg[copyLen-1] == '\r' || msg[copyLen-1] == '\n')) {
        msg[--copyLen] = '\0';
    }

    rttDebug.print("[MQTT← Topic] ");
    rttDebug.println(topic);
    rttDebug.print("[MQTT←] ");
    rttDebug.println(msg);

    const char* validCmds[] = {
        "SYSTEM_ON", "SYSTEM_OFF",
        "FAN_ON",    "FAN_OFF",
        "PISTON_OPEN", "PISTON_CLOSE"
    };
    for (auto& c : validCmds) {
        if (strcmp(msg, c) == 0) {
            // Forward immediately (UART write is fast, ~1ms)
            SerialA.println(msg);
            rttDebug.print("[FWD→A] ");
            rttDebug.println(msg);
            // FLAW-6 FIX: queue ACK for deferred publish
            char ackBuf[80];
            snprintf(ackBuf, sizeof(ackBuf), "{\"forwarded\":\"%s\"}", msg);
            queueAckPublish(ackBuf);
            return;
        }
    }
    rttDebug.println("[MQTT←] Unknown command, ignored.");
}

// ============================================================
//  buildMqttClientId()
//  Build a unique client ID from the modem IMEI.
//  Two devices with the same ID will continuously kick each other
//  off the broker. IMEI is guaranteed unique per SIM module.
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
    // Build a unique client ID from the modem IMEI.
    // Two devices with the same ID will continuously kick each other
    // off the broker. IMEI is guaranteed unique per SIM module.
    String imei = modem.getIMEI();
    imei.trim();
    if (imei.length() >= 8) {
        // Use last 8 digits of IMEI — still globally unique enough
        // for a fleet and avoids leaking the full IMEI in plain text.
        return String(MQTT_CLIENT_ID_PREFIX) + "_" + imei.substring(imei.length() - 8);
    }
    // Fallback: prefix + compile-time hash of __TIME__ (different per build)
    String t = String(__TIME__);
    t.replace(":", "");
    return String(MQTT_CLIENT_ID_PREFIX) + "_" + t;
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
    mqttClient.setSocketTimeout(45);   // Tăng lên 45 giây cho mạng GPRS yếu

    String clientId = buildMqttClientId();

    // Thêm lệnh RESET modem nếu thử lại nhiều lần không được
    static int failCounter = 0;
    if (failCounter > 5) {
        rttDebug.println("[MQTT] Qua nhieu lan timeout (state=-4), Reset Modem...");
        modem.restart();
        failCounter = 0;
        return;
    }

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

        // Drain any modem-buffered bytes
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

        // BUG-C3 FIX: Last Will and Testament.
        // When gateway disconnects uncleanly, broker publishes
        // {"status":"offline"} to .../sensors so dashboard knows.
        bool ok;
        if (strlen(MQTT_USER) > 0) {
            ok = mqttClient.connect(
                clientId.c_str(), MQTT_USER, MQTT_PASS,
                mqttTopicSensors,    // LWT topic
                1,                   // LWT QoS 1
                true,                // LWT retain
                "{\"status\":\"offline\"}");  // LWT message
        } else {
            ok = mqttClient.connect(
                clientId.c_str(),
                mqttTopicSensors,    // LWT topic
                1,                   // LWT QoS 1
                true,                // LWT retain
                "{\"status\":\"offline\"}");  // LWT message
        }

        if (ok) {
            failCounter = 0;
            rttDebug.println(" OK");
            // BUG-C2 FIX: Subscribe with QoS 1 for reliable command delivery
            // BUG-C5 FIX: Check subscribe return value
            if (!mqttClient.subscribe(mqttTopicControl, 1)) {
                rttDebug.println("[MQTT] WARNING: Subscribe to control topic FAILED");
            }
            // BUG-C4 FIX: Retain online status so new subscribers see it
            mqttClient.publish(mqttTopicSensors,
                "{\"status\":\"online\",\"device\":\"STM32B_GW\"}", true);
            return;   // successfully connected
        }

        failCounter++;
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
    // L3 FIX: setTimeout no longer needed — main.cpp now uses
    // non-blocking character accumulator instead of readStringUntil().
    delay(100);

    SerialSIM.begin(SIM_BAUD);

    rttDebug.print("[SIM] Waiting for modem cold-boot (8s)");
    for (int i = 0; i < 80; i++) { delay(100); rttDebug.print("."); }
    rttDebug.println();

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

    // Build per-device MQTT topics using last 8 digits of IMEI
    {
        String imei = modem.getIMEI();
        imei.trim();
        String devId = (imei.length() >= 8)
            ? imei.substring(imei.length() - 8)
            : "unknown";
        snprintf(mqttTopicSensors, sizeof(mqttTopicSensors),
                 MQTT_TOPIC_BASE "/%s/sensors", devId.c_str());
        snprintf(mqttTopicControl, sizeof(mqttTopicControl),
                 MQTT_TOPIC_BASE "/%s/control", devId.c_str());
        snprintf(mqttTopicAck, sizeof(mqttTopicAck),
                 MQTT_TOPIC_BASE "/%s/ack",     devId.c_str());
        snprintf(mqttTopicHeartbeat, sizeof(mqttTopicHeartbeat),
                 MQTT_TOPIC_BASE "/%s/heartbeat", devId.c_str());
        rttDebug.print("[MQTT] Device topics prefix: " MQTT_TOPIC_BASE "/");
        rttDebug.println(devId);
    }

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

        if (modem.gprsConnect(SIM_APN_NAME, SIM_APN_USER, SIM_APN_PASS)) {
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
        if (modem.gprsConnect(SIM_APN_NAME, SIM_APN_USER, SIM_APN_PASS)) {
            gprsConnected = true;
            rttDebug.println("[SIM] GPRS reconnected.");
            forwardCommandToA("INFO:GPRS_RECONNECTED");
        } else {
            forwardCommandToA("ERR:GPRS_RECONNECT_FAIL");
            // PHASE 3 FIX: If GSM base (not just GPRS context) is also lost,
            // gprsConnect() will fail forever. modem.restart() forces a fresh
            // network search — escapes the "Dead Zombie Gateway" trap.
            if (!modem.isNetworkConnected()) {
                rttDebug.println("[SIM] GSM base lost — restarting modem for fresh network search...");
                modem.restart();
                forwardCommandToA("ERR:GSM_RESTART");
            }
        }
    }
}

// ============================================================
//  publishSensorMQTT()
// ============================================================
//  FLAW-3 FIX: onDataFromA() — dumb JSON relay.
//  STM32-A now sends the JSON payload directly.
//  STM32-B publishes it verbatim — no CSV parsing needed.
//  Removed: publishSensorMQTT(), parseCSVField() (152 lines)
// ============================================================
void onDataFromA(const char* jsonPayload) {
    rttDebug.print("[DATA→] ");
    rttDebug.println(jsonPayload);

    if (!mqttClient.connected()) {
        rttDebug.println("[MQTT] Not connected — buffering payload.");
        bufferPush(jsonPayload);
        return;
    }
    // Flush any buffered entries first, then publish current
    flushBuffer();

    if (mqttClient.publish(mqttTopicSensors, jsonPayload)) {
        rttDebug.println("[MQTT→] Published.");
    } else {
        rttDebug.println("[MQTT→] Publish failed.");
    }
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
//  FLAW-2 FIX: onAlertFromA()
//  Phone number now comes from ALERT_PHONE_NUMBER in config.h.
//  STM32-A sends just the message body.
// ============================================================
void onAlertFromA(const String& message) {
    rttDebug.print("[ALERT] msg=");
    rttDebug.println(message);

    String body = "[" ALERT_PHONE_NUMBER "] " + message;

    if (sendHttpAlert(body)) {
        rttDebug.println("[ALERT] Sent OK.");
        queueAckPublish("{\"alert\":\"sent\"}");
    } else {
        rttDebug.println("[ALERT] Failed.");
    }

    // PHASE 3 FIX: Drain UART bytes that buffered during HTTP blocking (up to 15s).
    // STM32-A sends a DATA: payload every 10s — its 106-byte frame overflows the
    // 64-byte hardware RX buffer while we are inside sendHttpAlert(). Discarding
    // the stale bytes prevents the gateway from parsing a corrupted half-line.
    while (SerialA.available()) SerialA.read();

    // BUG-C6 FIX: After HTTP alert (up to 15s blocking), immediately
    // call mqttClient.loop() to refresh the keepalive timer and process
    // any commands that arrived during the HTTP window.
    if (mqttClient.connected()) {
        mqttClient.loop();
    }
}

// ============================================================
//  LATENCY FIX L9: Non-blocking MQTT reconnect state machine.
//
//  The old connectMQTT() blocked loop() for up to 160 seconds
//  (3 attempts × 45s timeout + delays). This state machine
//  performs ONE step per loopGateway() call:
//    IDLE → CLOSE_SOCKET → TCP_CONNECT → STABILISE → MQTT_HANDSHAKE → DONE
//  Between steps, loop() runs freely for UART reads, LED blink, etc.
//
//  connectMQTT() remains for the initial boot-time connection
//  (blocking is acceptable during setup).
// ============================================================

// Reconnect state machine
enum MqttReconnState {
    MR_IDLE,
    MR_CLOSE_SOCKET,
    MR_TCP_CONNECT,
    MR_STABILISE,
    MR_DRAIN,
    MR_MQTT_HANDSHAKE,
};

static MqttReconnState mrState      = MR_IDLE;
static unsigned long   mrTimestamp  = 0;
static int             mrAttempt    = 0;
static int             mrFailCount  = 0;
static String          mrClientId;

// One step of the non-blocking reconnect. Returns true when done
// (either connected or all attempts exhausted).
static bool stepMqttReconnect() {
    unsigned long now = millis();

    switch (mrState) {
    case MR_IDLE:
        // Reset modem after too many total failures
        if (mrFailCount > 5) {
            rttDebug.println("[MQTT] Too many failures, resetting modem...");
            modem.restart();
            mrFailCount = 0;
            return true;  // done for this cycle, loopGateway will retry
        }
        mrAttempt = 0;
        mrClientId = buildMqttClientId();
        mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
        mqttClient.setCallback(mqttCallback);
        mqttClient.setBufferSize(512);
        mqttClient.setKeepAlive(MQTT_KEEPALIVE);
        mqttClient.setSocketTimeout(15);  // Reduced from 45s — non-blocking retries are fast
        mrState = MR_CLOSE_SOCKET;
        return false;

    case MR_CLOSE_SOCKET:
        mrAttempt++;
        if (mrAttempt > 3) {
            rttDebug.println("[MQTT] All 3 attempts failed.");
            mrState = MR_IDLE;
            return true;
        }
        rttDebug.print("[MQTT] Attempt ");
        rttDebug.print(mrAttempt);
        rttDebug.println("/3");
        if (gsmClientMQTT.connected()) {
            gsmClientMQTT.stop();
        }
        mrTimestamp = now;
        mrState = MR_TCP_CONNECT;
        return false;

    case MR_TCP_CONNECT:
        // Wait 500ms after socket close before new TCP connect
        if (now - mrTimestamp < 500) return false;
        rttDebug.println("[MQTT] TCP connect...");
        if (!gsmClientMQTT.connect(MQTT_BROKER, MQTT_PORT)) {
            rttDebug.println("[MQTT] TCP FAILED");
            mrTimestamp = now;
            mrState = MR_CLOSE_SOCKET;  // retry next attempt (after delay in loopGateway)
            return false;
        }
        rttDebug.println("[MQTT] TCP OK — stabilising...");
        mrTimestamp = now;
        mrState = MR_STABILISE;
        return false;

    case MR_STABILISE:
        // Wait 2s for A7680C channel to come up
        if (now - mrTimestamp < 2000) return false;
        mrState = MR_DRAIN;
        return false;

    case MR_DRAIN:
        // Drain stale modem bytes
        while (gsmClientMQTT.available()) gsmClientMQTT.read();
        mrState = MR_MQTT_HANDSHAKE;
        return false;

    case MR_MQTT_HANDSHAKE: {
        rttDebug.print("[MQTT] CONNECT as ");
        rttDebug.print(mrClientId);
        rttDebug.print("...");
        // BUG-C3 FIX: LWT for non-blocking reconnect path too
        bool ok;
        if (strlen(MQTT_USER) > 0) {
            ok = mqttClient.connect(
                mrClientId.c_str(), MQTT_USER, MQTT_PASS,
                mqttTopicSensors, 1, true,
                "{\"status\":\"offline\"}");
        } else {
            ok = mqttClient.connect(
                mrClientId.c_str(),
                mqttTopicSensors, 1, true,
                "{\"status\":\"offline\"}");
        }
        if (ok) {
            mrFailCount = 0;
            rttDebug.println(" OK");
            // BUG-C2 + C5 FIX
            if (!mqttClient.subscribe(mqttTopicControl, 1)) {
                rttDebug.println("[MQTT] WARNING: Subscribe FAILED");
            }
            // BUG-C4 FIX: retained online status
            mqttClient.publish(mqttTopicSensors,
                "{\"status\":\"online\",\"device\":\"STM32B_GW\"}", true);
            mrState = MR_IDLE;
            return true;  // connected!
        }
        mrFailCount++;
        rttDebug.print(" FAILED state=");
        rttDebug.println(mqttClient.state());
        gsmClientMQTT.stop();
        mrState = MR_CLOSE_SOCKET;  // retry next attempt
        return false;
    }
    }
    return true;
}

static bool mqttReconnecting = false;

void loopGateway() {
    unsigned long now = millis();

    // Periodic GPRS health check
    if (now - lastGprsCheckMs >= GPRS_CHECK_INTERVAL) {
        lastGprsCheckMs = now;
        maintainGPRS();
    }

    if (!gprsConnected) return;

    // L9 FIX: Non-blocking MQTT reconnect
    if (!mqttClient.connected()) {
        if (!mqttReconnecting) {
            // Start reconnect only on the timer interval
            if (now - lastMqttReconnectMs >= MQTT_RECONNECT_INTERVAL) {
                lastMqttReconnectMs = now;

                // Check GPRS health first
                if (!modem.isGprsConnected()) {
                    rttDebug.println("[MQTT] GPRS lost — recovering");
                    gprsConnected = false;
                    maintainGPRS();
                    return;
                }

                rttDebug.println("[MQTT] Disconnected — starting reconnect...");
                mqttReconnecting = true;
                mrState = MR_IDLE;
            }
        } else {
            // Advance state machine one step per loop()
            if (stepMqttReconnect()) {
                mqttReconnecting = false;
            }
        }
        return;
    }

    // L1 FIX: mqttClient.loop() is always called when connected
    mqttClient.loop();
}
