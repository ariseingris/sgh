# Smart Greenhouse - Sensor Reading Fixes

## Issues Fixed

### 1. **Critical Logic Error - Conflicting Activation/Deactivation**
**Problem:** 
```cpp
activate_System();      // Activate
deactivate_System();    // Immediately deactivate
```
This caused continuous toggling of relays and actuators every loop cycle.

**Solution (current — state machine):** Replaced `systemActive` boolean and `activate/deactivate_system()` with a three-state machine in `gadget.cpp`:

```
STATE_IDLE  ──enterMeasuring()──▶  STATE_MOVING  ──piston stops, fan ON──▶  STATE_MEASURING
STATE_MEASURING  ──enterIdle()──▶  STATE_MOVING  ──piston stops, fan OFF──▶  STATE_IDLE
```

- `enterMeasuring()` / `enterIdle()` — called by UART commands (`SYSTEM_ON`/`SYSTEM_OFF`), RTT keys `1`/`2`, and the PB12 toggle button.
- `update_actuators()` (called every `loop()`) stops the piston after `PISTON_RUN_MS` (8 s) and drives the `MOVING → MEASURING/IDLE` transition based on fan state.
- `isMeasuring()` — returns true only in `STATE_MEASURING`; gates `sendDataToB()` and alert sending.
- The old `g_dataReady` gating described below is **historical** — it no longer drives system state.

---

### 2. **Variable Shadowing**
**Problem:** `unsigned long currentMillis` was declared globally, then redeclared locally in loop()

**Solution:** 
- Removed global `currentMillis` declaration
- Declared locally only where needed
- Added separate timing variables: `last_sensor_read` and `last_period_message`

---

### 3. **Missing SIM Module Setup**
**Problem:** `setupSIM_A7680()` was never called in setup()

**Solution:** Added SIM initialization in setup() after sensor initialization:
```cpp
Serial.println("DANG KHOI TAO SIM MODULE...");
setupSIM_A7680();
delay(500);
```

---

### 4. **Sensor Reading Timing Issues**
**Problem:** All sensors read every loop without synchronization, potentially causing I2C conflicts

**Solution:** Created `readAllSensors()` function with interval-based reading:
```cpp
// Only read sensors at the specified interval (2 seconds)
if (currentMillis - last_sensor_read < interval_sensor_read) {
    return;
}
```
- Analog sensors (Gas, Soil Moisture) read first (no I2C conflicts)
- I2C sensors (Light, Temp/Humidity, Pressure) read sequentially
- CO2 sensor only read when data is ready

---

### 5. **Improved Actuator Control**
**Changes:**
- Added automatic delays in `retractActuator()` and `extendActuator()` (1 second)
- Prevents pin conflict with improper timing
- Removed excessive delays from activate/deactivate functions

---

### 6. **Better System Organization**

#### Timing Settings (Configurable)
```cpp
const unsigned long interval_message = 3600000;      // SMS every 1 hour
const unsigned long interval_sensor_read = 2000;     // Sensor read every 2 seconds
```

#### System State Tracking
```cpp
bool systemActive = false;           // Current system state
bool systemInitialized = false;      // Initialization complete
bool g_dataReady = false;            // CO2 sensor ready
```

---

## Main Loop Flow (current)

```
Loop Start
    ↓
Pet watchdog (IWatchdog.reload())
    ↓
Read All Sensors (every 5 s)
    ↓
Receive commands from STM32-B (non-blocking UART)
    → SYSTEM_ON  → enterMeasuring() → STATE_MOVING → (piston retracts, fan ON)
    → SYSTEM_OFF → enterIdle()      → STATE_MOVING → (piston extends, fan OFF)
    → FAN_ON/OFF, PISTON_OPEN/CLOSE — direct actuator calls
    ↓
Send DATA: to STM32-B (every 10 s, only while STATE_MEASURING)
    ↓
update_actuators() — auto-stop piston after 8 s; transitions MOVING → MEASURING/IDLE
    ↓
check_PhysicalButtons() — edge-triggered debounce
    → PA7 → extend_Piston()
    → PB0 → retract_Piston()
    → PB12 → enterMeasuring() / enterIdle() toggle
    ↓
Periodic RTT sensor dump (every 10 s)
    ↓
Periodic alert via STM32-B (every 1 h, only while STATE_MEASURING + CO2 ready)
    ↓
Loop
```

---

## Testing Checklist

- [ ] Verify all sensors read correctly (check Serial Monitor)
- [ ] Confirm system activates only when CO2 data is ready
- [ ] Verify piston/fan control without relay conflicts
- [ ] Test SMS alerts every hour when system is active
- [ ] Monitor I2C communication (no bus conflicts)

---

## Sensor Reading Order (Optimized)

1. **Analog Sensors** (PA0, PA2) - No I2C conflicts
   - Gas Sensor (MQ-4)
   - Soil Moisture

2. **I2C Sensors** (Address: 0x44, 0x62, 0x76)
   - Light Sensor (BH1750)
   - Temperature/Humidity (SHT30)
   - Pressure (BME280)

3. **Delayed Read**
   - CO2 Sensor (SCD40) - Only when ready flag is true

---

## Next Steps

1. Compile and upload to your STM32 board
2. Monitor Serial output at 115200 baud
3. Verify each sensor initializes successfully
4. Test system activation/deactivation logic
5. Verify SMS alerts are sent periodically
6. Monitor I2C communication (no bus conflicts)

---

**Review (concise)**

- **System overview:** STM32-A (sensor/actuator node) sends JSON over UART to STM32-B (gateway). STM32-B publishes sensor JSON to an MQTT broker and posts alerts to ntfy.sh. The gateway subscribes to a control topic and forwards commands to STM32-A via UART.

**Visual Workflow**

STM32-A (sgh_main/src/main.cpp)
    - Read sensors → Build JSON → Send `DATA:{...}` over UART
    - Send `ALERT:<msg>` over UART when alert condition
    - Receive commands via UART (e.g., `SYSTEM_ON`, `FAN_ON`) and ACK
                |
                v
STM32-B Gateway (sgh_sup)
    - Non-blocking UART reader parses `DATA:`, `ALERT:`, `ACK:` lines
    - `DATA:` → published to MQTT topic for device
    - `ALERT:` → posted to ntfy.sh (alerting path)
    - MQTT callback → valid commands forwarded to STM32-A via UART
                |
                v
MQTT Broker (HiveMQ Cloud)
    - Receives sensor publishes (for dashboard/clients)
    - Sends control messages to gateway (subscribed topic)

**Critical Issues (high priority)**

- **Secrets in repo:** `sgh_sup/src/config.h` may contain broker credentials and phone numbers in cleartext — critical exposure risk.
- **TLS/port mismatch risk:** `MQTT_PORT` may be set to 8883 while TLS support in the modem/client is not guaranteed — this causes silent connection failures.
- **Blocking alert path risk:** If HTTP alerting is performed synchronously inside UART handling, long POSTs can corrupt UART parsing or delay keepalive; alerts should be offloaded from the UART path.
- **MQTT QoS assumptions:** Code uses QoS 1 in some places (subscribe/LWT) but mobile/GPRS + PubSubClient behavior may not guarantee QoS1 delivery semantics — verify requirements.
- **Buffer overflow / data loss:** Fixed-size ring buffers for sensor payloads and ACKs overwrite or drop entries when full; prolonged broker outage can cause silent loss.
- **Hard-coded routing assumptions:** Phone numbers and ntfy topics are stored in gateway-side config; mismatch between devices or accidental commits can break alerts.
- **Macro fragility:** `#define Serial rttDebug` must remain after all library includes; moving or refactoring may break TinyGSM or other libraries.
- **Lack of telemetry:** No dedicated diagnostic/troubleshooting MQTT topic for queue sizes, modem status, or failure reasons — remote debugging is harder.

---

If you'd like, I can (A) add this content to a standalone `REVIEW.md`, or (B) open a non-invasive PR that only documents these issues without changing code. Which do you prefer?
