# STM32-B Gateway | RTT Debug Setup & HiveMQ Connection

## Quick Start

### 1. Build and Upload
```bash
# Build the project
platformio run --environment bluepill_f103c8

# Upload to device
platformio run --target upload --environment bluepill_f103c8
```

### 2. Start RTT Debug Console
```bash
# Use the improved script (RECOMMENDED)
chmod +x run_rtt_fixed.sh
./run_rtt_fixed.sh

# OR use the original (less stable on some systems)
chmod +x run_rtt.sh
./run_rtt.sh
```

You should see debug output like:
```
╔════════════════════════════════════════════════════════╗
║  STM32-B GATEWAY NODE - Starting up                   ║
║  Connected via SEGGER RTT + ST-Link                   ║
╚════════════════════════════════════════════════════════╝
[SETUP] Initializing SIM7600 modem...
```

## Architecture

```
STM32-B (Blue Pill) Gateway
├── USART1 (PA9/PA10) ──> SIM7600 Modem 
│   └──> GPRS/MQTT Connection to HiveMQ
├── USART2 (PA3 RX / PA2 TX) ──> STM32-A Sensor Node
│   └──> Receives sensor data via "DATA:" protocol
└── ST-Link Debugger
    └──> SEGGER RTT debugging via port 19021
```

## MQTT Connection Flow

1. **SIM Module Initialization** (8-15 seconds)
   - Waits for modem bootup
   - Tests AT commands  
   - Probes for GSM network signal

2. **Network Registration** (up to 60 seconds)
   - Detects Vietnamobile network (m3-world APN)
   - Activates GPRS/LTE connection

3. **MQTT Handshake** (1-5 seconds)
   - Connects to broker.hivemq.com:1883
   - Subscribes to `greenhouse/stm32/control`
   - Publishes online status

## Troubleshooting

### Problem: "No output in RTT console"
**Causes:**
- ST-Link not properly connected
- USB permissions issue
- OpenOCD not finding the board

**Solutions:**
```bash
# Fix USB permissions
sudo chmod -R 777 /dev/bus/usb/

# Verify ST-Link is detected
lsusb | grep ST-Link

# Check OpenOCD logs
cat /tmp/openocd_rtt.log
```

### Problem: "MQTT Connection Failed" (error code -2)
**Causes:**
- No GPRS connection (modem not connected to network)
- Power supply insufficient (SIM modules need 2A)
- DNS resolution failing

**Debug steps in RTT output:**
```
[SIM] Waiting for GSM network...
[SIM] GSM OK. Signal: 20        ← If missing, no network
[SIM] Connecting GPRS...
[SIM] GPRS OK. IP: 10.x.x.x     ← If missing, GPRS failed
[MQTT→] Connecting to broker.hivemq.com:1883...
[MQTT✗] CONNECTION FAILED! (error code: -2) - MQTT_CONNECT_FAILED
```

**Check:**
1. SIM card has active data plan
2. Power supply provides stable 2A at 3.3V
3. RX/TX pins PA9(SIM RX) and PA10(SIM TX) wired correctly

### Problem: "MQTT Connected but no data flowing"
**Likely issue:**
- HiveMQ topics not configured in firmware

**Check:**
```bash
# View MQTT topics with mosquitto client
mosquitto_sub -h broker.hivemq.com -t "greenhouse/#" -v
```

Expected published topics:
- `greenhouse/stm32/status` - Gateway online/offline
- `greenhouse/stm32/sensors` - Sensor data from STM32-A
- `greenhouse/stm32/ack` - Command acknowledgments

### Problem: "LED PC13 not blinking"
**Indicates:**
- Main loop frozen (likely in SIM initialization)
- Check power supply and modem wiring

**Check logs for:**
- "[SIM] Modem alive." 
- "[SIM] GPRS OK."

## Configuration Files

### platformio.ini
- `debug_tool = stlink` - Enables ST-Link debugging
- `monitor_port = socket://localhost:19021` - RTT console address
- Build includes proper STM32F103xB flags

### module_sim.h
- `MQTT_BROKER = "broker.hivemq.com"` - Public test broker (no auth)
- `MQTT_RECONNECT_INTERVAL = 5000` - Retry every 5 seconds
- `SIM_NETWORK_TIMEOUT_MS = 60000` - Wait max 60s for network

### SEGGER_RTT_Conf.h
- `BUFFER_SIZE_UP = 1024` - Debug output buffer (target→host)
- `BUFFER_SIZE_DOWN = 16` - Command input buffer (host→target)
- RTT channels configured for optimal performance

## Advanced: Manual RTT Testing

If the scripts don't work, you can manually set up RTT:

```bash
# Terminal 1: Start OpenOCD
openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c telnet_port\ 4444

# Terminal 2: Configure RTT (in separate shell)
(echo "rtt setup 0x20000000 0x5000 \"SEGGER RTT\""; sleep 0.5; \
 echo "rtt start"; sleep 0.5; \
 echo "rtt server start 19021 0"; sleep 0.5; \
 echo "exit") | nc localhost 4444

# Terminal 3: Connect to RTT console
nc localhost 19021
# Or: telnet localhost 19021
```

## Performance Notes

- **Heartbeat interval**: 30 seconds (configurable in main.cpp)
- **Fake data test interval**: 10 seconds (for debugging)
- **LED blink rate**: 500ms (indicates loop is running)
- **MQTT keep-alive**: 60 seconds 
- **GPRS watchdog check**: 5 minutes

## Next Steps

1. Verify RTT output shows full initialization sequence
2. Check `greenhouse/#` topics in HiveMQ for published data
3. Send test commands to `greenhouse/stm32/control` topic
4. Verify STM32-A sensor node data reaches gateway

## Support

For issues:
1. Enable full debug output: `Serial.print()` statements remain in RTT
2. Check /tmp/openocd_rtt.log for OpenOCD errors
3. Verify hardware wiring matches module_sim.h PIN definitions
4. Test MQTT manually with mosquitto clients
