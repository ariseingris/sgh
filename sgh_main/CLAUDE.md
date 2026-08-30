# sgh_main — STM32-A Sensor Node

## What this project is

Firmware for the **sensor + actuator node** (STM32-A) of a Smart Greenhouse
system. Runs on a STM32F103C8 Blue Pill via PlatformIO + Arduino framework.

STM32-A reads six environmental sensors, controls a fan relay and a linear
piston actuator, and sends validated sensor data over UART to STM32-B
(the gateway node in `sgh_sup/`) every 10 seconds.

---

## Hardware

| Item | Detail |
|------|--------|
| MCU | STM32F103C8T6 Blue Pill — 64 KB flash, 20 KB RAM |
| Debug | SEGGER RTT via ST-Link SWD (port 19021) |
| UART to B | USART1 — PA9 TX → STM32-B PA10 RX, PA10 RX ← STM32-B PA9 TX |
| I2C bus | PB6 SCL / PB7 SDA @ 400 kHz |
| Upload | ST-Link v2 via SWD |

### Sensors

| Sensor | Bus | Address | Variable |
|--------|-----|---------|----------|
| BH1750 — light | I2C | 0x23 | `g_lux` |
| SHT30 — temp/humidity | I2C | 0x44 (fallback 0x45) | `g_temperature`, `g_humidity` |
| SCD40 — CO₂ | I2C | 0x62 | `g_co2`, `g_co2Ready` |
| BME280 — pressure | I2C | 0x76 | `g_pressure` |
| MQ-4 — gas (methane) | ADC | PB1 | `g_gasValue` |
| Capacitive soil moisture | ADC | PA0 | `g_soilMoist` |

### Actuators

| Pin | Role |
|-----|------|
| PA1 | Fan relay |
| PA3 | Piston IN1 (H-bridge) |
| PA4 | Piston IN2 (H-bridge) |
| PA7 | Physical button — OPEN (deactivate system) |
| PB0 | Physical button — CLOSE (activate system) |

---

## Source file map

```
src/
├── main.cpp           — setup(), loop(), UART protocol, timing, I2C watchdog
├── gadget.cpp/h       — fan + piston control, physical button debounce
├── sensor_light.cpp/h — BH1750 (continuous high-res mode)
├── sensor_temp_hum.cpp/h — SHT30
├── sensor_co2.cpp/h   — SCD40 (async data-ready polling)
├── sensor_pressure.cpp/h — BME280
├── sensor_gas.cpp/h   — MQ-4 ADC, warm-up guard
├── sensor_soil.cpp/h  — capacitive ADC, maps to 0–100 %
├── i2c_recover.h      — inline I2C bus recovery (clocks SCL 9×, then Wire.begin())
├── rtt_debug.h        — RTTSerial class wrapping SEGGER RTT channel 0
├── SEGGER_RTT_Conf.h  — RTT buffer sizes (tuneable)
├── SEGGER_RTT.c/h     — VENDOR — do not edit
└── config.h           — secrets (gitignored — never commit)
```

---

## Data flow

```
Sensors (I2C + ADC)
    ↓  every 5 s
main.cpp readAllSensors()
    ↓
sendDataToB()  — every 10 s
    ↓  UART USART1 115200 baud
"DATA:<temp>,<hum>,<co2>,<lux>,<pressure>,<gas>,<soil>\n"
    ↓
STM32-B (sgh_sup) → HiveMQ → Web SaaS
```

Inbound commands from STM32-B arrive on `SerialB` and are parsed in
`parseBCommand()`. Valid commands: `SYSTEM_ON`, `SYSTEM_OFF`, `FAN_ON`,
`FAN_OFF`, `PISTON_OPEN`, `PISTON_CLOSE`.

---

## Key timing constants

| Constant | Value | Purpose |
|----------|-------|---------|
| `SENSOR_INTERVAL` | 5000 ms | How often sensors are read |
| `DATA_INTERVAL` | 10000 ms | How often DATA: is sent to STM32-B |
| `PRINT_INTERVAL` | 10000 ms | How often RTT prints sensor dump |
| `ALERT_INTERVAL` | 3600000 ms | How often ALERT is sent (1 hour) |
| `PISTON_RUN_MS` | 8000 ms | Auto-stop piston after 8 s |
| `DEBOUNCE_MS` | 300 ms | Physical button debounce |
| MQ-4 warm-up | 300000 ms | Minimum before gas readings are valid |

---

## RTT debug console

```bash
chmod +x run_rtt_fixed.sh
./run_rtt_fixed.sh
```

RTT terminal commands (press key, then Enter):

| Key | Action |
|-----|--------|
| `1` | Activate system (fan ON, piston CLOSE) |
| `2` | Deactivate system (fan OFF, piston OPEN) |
| `3` | Print current sensor readings |
| `4` | Request alert via STM32-B |
| `5` | Send real sensor DATA: to STM32-B now |
| `6` | Send fake hardcoded DATA: for pipeline testing |

---

## Build and upload

```bash
# Build only
pio run

# Build and flash via ST-Link
pio run -t upload

# Flash size budget — must stay under 65536 bytes
# Current: ~63900 bytes (97.5%). Add -Os to build_flags if over budget.
```

---

## Critical rules — read before editing any file

### 1. Never use `Serial.print()` in sensor files

`Serial` is remapped to `rttDebug` via `#define Serial rttDebug` — but that
macro lives **only in `main.cpp`**, placed after all `#include`s. It does not
exist in sensor files or `gadget.cpp`. In those files, use `rttDebug` directly:

```cpp
extern RTTSerial rttDebug;
rttDebug.println("[SENSOR] error message");
```

Placing `#define Serial rttDebug` in `rtt_debug.h` would corrupt TinyGSM and
Arduino library internals — do not do it.

### 2. Never modify `SEGGER_RTT.c` or `SEGGER_RTT.h`

These are SEGGER vendor files. The RTT protocol requires byte-exact
compatibility with the J-Link / OpenOCD host side. Edit `SEGGER_RTT_Conf.h`
for buffer size changes.

### 3. Every sensor must have a `Ready` flag and return `-999.0f` on failure

`-999.0f` is the fault sentinel for all float sensors. It is well outside
every physical range and allows the cloud dashboard to display "N/A" instead
of misreading a failure as a freeze event (0 °C / 0% humidity).

Pattern:
```cpp
static bool sensorReady = false;

void setupXxx() {
    if (!device.begin()) { sensorReady = false; return; }
    sensorReady = true;
}

float readXxx() {
    if (!sensorReady) return -999.0f;
    float val = device.read();
    if (isnan(val) || val < MIN || val > MAX) return -999.0f;
    return val;
}
```

`checkI2CHealth()` detects bus lockup by checking `g_pressure < 0.0f` and
`g_temperature < -100.0f` (real atmospheric pressure is always positive;
real temperature is always ≥ -40 °C).

NaN or out-of-range values must never reach `sendDataToB()`.

### 4. `config.h` is gitignored — never hardcode secrets

Phone numbers, broker credentials, and APN settings go in `src/config.h`.
That file is listed in `.gitignore` and `.claudeignore`. The template is
`src/config.h.example`.

### 5. Piston auto-stop is non-negotiable

`extend_Piston()` and `retract_Piston()` set a timestamp. `update_actuators()`
(called every loop) stops the piston after `PISTON_RUN_MS` (8 s). Never add
blocking `delay()` inside actuator functions — it stalls UART receive and the
piston auto-stop timer.

### 6. I2C clock speed must be 400 kHz

`Wire.setClock(400000)` must appear immediately after `Wire.begin()` in
`setup()`. Without it, four sensors on the same bus at 100 kHz default cause
timeouts under simultaneous read load.

### 7. Watchdog must be fed every loop

`IWatchdog.reload()` is the first statement in `loop()`. Do not add any
blocking code before it. If a new blocking operation is necessary, break it
into non-blocking state-machine steps.

---

## I2C bus recovery

`checkI2CHealth()` (called after every sensor read cycle) detects consecutive
fault sentinels from both the BME280 (`g_pressure < 0`) and SHT30
(`g_temperature < -100`). If both fault for `I2C_FAIL_THRESHOLD` (3) consecutive
cycles, it calls `recoverI2C()`
from `i2c_recover.h`:

1. Switches SDA/SCL pins to GPIO mode
2. Clocks SCL up to 9 times until SDA goes HIGH (releases stuck device)
3. Sends a manual STOP condition
4. Calls `Wire.end()` → `delay(10)` → `Wire.begin()` → `Wire.setClock(400000)`
5. Re-initialises all four I2C sensors

Recovery is rate-limited to once per 30 seconds (`I2C_RECOVER_COOLDOWN`).

---

## DATA: protocol (UART to STM32-B)

```
Format:  DATA:{JSON}   (STM32-B publishes verbatim — no parsing)
Example: DATA:{"ts":12345,"temp":26.5,"hum":65.0,"co2":450,
               "lux":1200.0,"pressure":1012.5,"gas":280,"soil":60,
               "fan":1,"piston":0}

Field    Sensor   Type     Valid range      Fault sentinel
temp     SHT30    float    -40 to 125 °C   -999.0 = I2C error
hum      SHT30    float    0 to 100 %RH    -999.0 = I2C error
co2      SCD40    uint16   0 to 5000 ppm   0 = not ready (see g_co2Ready)
lux      BH1750   float    0 to 65000 lux  -999.0 = I2C error
pressure BME280   float    100 to 1200 hPa -999.0 = I2C error
gas      MQ-4     int      0 to 4095 ADC   -1 = warming up (first 5 min)
soil     ADC      int      0 to 100 %      no sentinel (always valid)
fan               int      0 or 1          0 = OFF, 1 = ON
piston            int      0 or 1          0 = extended/open, 1 = retracted/closed
```

---

## Flash budget

Target: stay under **62000 bytes** to leave 3 KB headroom for fixes.

If `pio run` reports > 62000 bytes:
1. Confirm `-Os` is in `build_flags` (optimize for size)
2. Check if any `String` concatenation was added — replace with `snprintf`
3. Check if `SEGGER_RTT_printf.c` was accidentally added — keep only
   `SEGGER_RTT.c` (the `printf` variant adds ~4 KB)

Current baseline after `-Os`: ~58000–60000 bytes depending on toolchain version.

---

## lib_deps (platformio.ini)

```ini
lib_deps =
    claws/BH1750 @ ^1.3.0
    adafruit/Adafruit SHT31 Library @ ^2.2.2
    sensirion/Sensirion I2C SCD4x @ ^0.4.0
    adafruit/Adafruit BME280 Library @ ^2.2.4
    adafruit/Adafruit Unified Sensor @ ^1.1.14
```

Do not upgrade these versions without testing — the SCD4x library API changed
between 0.3.x and 0.4.x.
