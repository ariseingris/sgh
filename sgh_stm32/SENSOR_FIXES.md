# Smart Greenhouse - Sensor Reading Fixes

## Issues Fixed

### 1. **Critical Logic Error - Conflicting Activation/Deactivation**
**Problem:** 
```cpp
activate_System();      // Activate
deactivate_System();    // Immediately deactivate
```
This caused continuous toggling of relays and actuators every loop cycle.

**Solution:** Implemented a state machine that only activates/deactivates based on sensor conditions:
- System activates when CO2 data is ready (`g_dataReady == true`)
- System deactivates when CO2 data becomes unavailable
- Guard conditions prevent repeated calls (check `if (systemActive)` before activating)

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

## Main Loop Flow

```
Loop Start
    ↓
Read All Sensors (interval-based)
    ↓
Check: CO2 Ready & System Inactive?
    → YES: Activate System (piston, fan)
    ↓
Check: System Active & CO2 Not Ready?
    → YES: Deactivate System
    ↓
Check: Send SMS? (hourly if system active)
    → YES: Send sensor data
    ↓
Update SIM Connection
    ↓
Small Delay (100ms)
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
