#!/bin/bash

set -e

# ============================================================
# STM32 Blue Pill RTT Debug Console Setup
# Connects via ST-Link for real-time debug output (HiveMQ Gateway)
# ============================================================

echo "═════════════════════════════════════════════════════════"
echo "  STM32-B Gateway | RTT Debugger Setup"
echo "═════════════════════════════════════════════════════════"

# 1. Find OpenOCD binary and scripts from platformio
echo "► Step 1: Locating OpenOCD..."
OPENOCD_BIN=$(find "$HOME/.platformio/packages/tool-openocd" -name "openocd" -type f 2>/dev/null | head -n 1)

if [ -z "$OPENOCD_BIN" ]; then
    echo "❌ ERROR: OpenOCD not found in platformio packages"
    echo "   Fix: Run 'platformio upgrade' or 'platformio run -t upload'"
    exit 1
fi

# Derive the scripts directory from the binary location
OPENOCD_DIR=$(dirname "$(dirname "$OPENOCD_BIN")")
OPENOCD_SCRIPTS="$OPENOCD_DIR/openocd/scripts"

if [ ! -d "$OPENOCD_SCRIPTS" ]; then
    echo "❌ ERROR: OpenOCD scripts not found at $OPENOCD_SCRIPTS"
    exit 1
fi

echo "   ✓ Found: $OPENOCD_BIN"
echo "   ✓ Scripts: $OPENOCD_SCRIPTS"

# 2. Clean up any lingering OpenOCD processes to avoid port conflicts
echo "► Step 2: Cleaning up..." 
killall -9 openocd > /dev/null 2>&1 || true
sleep 1

# 3. OpenOCD configuration for STM32F1x with ST-Link
# 4. Start OpenOCD server in background with logging
echo "► Step 3: Starting OpenOCD server..."
$OPENOCD_BIN \
    -s "$OPENOCD_SCRIPTS" \
    -f interface/stlink.cfg \
    -f target/stm32f1x.cfg \
    -c "telnet_port 4444" \
    -c "gdb_port disabled" \
    -c "tcl_port disabled" \
    -c "init" \
    > /tmp/openocd_rtt.log 2>&1 &
OPENOCD_PID=$!
sleep 3

# 5. Verify OpenOCD connected successfully
if ! ps -p $OPENOCD_PID > /dev/null; then
    echo "❌ ERROR: OpenOCD failed to start!"
    echo "   Possible causes:"
    echo "   • ST-Link not connected"
    echo "   • USB permissions issue"
    echo "   • Wrong board configuration"
    echo ""
    echo "   Fix USB permissions:"
    echo "   sudo chmod -R 777 /dev/bus/usb/"
    echo ""
    echo "   OpenOCD output:"
    cat /tmp/openocd_rtt.log
    exit 1
fi

echo "   ✓ OpenOCD running (PID: $OPENOCD_PID)"

# 6. Configure RTT channels (with retries for reliability)
# Each command is sent in a separate nc invocation so we wait for OpenOCD
# to fully process one command before sending the next.
echo "► Step 4: Configuring RTT..."
RTT_SETUP_OK=false
for attempt in 1 2 3; do
    echo "   Attempt $attempt/3..."

    # Step A: tell OpenOCD where the RTT control block lives
    echo "rtt setup 0x20000000 0x10000 \"SEGGER RTT\"" \
        | nc -w 3 localhost 4444 > /tmp/rtt_setup.log 2>/dev/null

    # Wait for OpenOCD to finish scanning RAM for the control block.
    # 2 s is enough even on slow targets; 0.3 s was the root cause of the bug.
    sleep 2

    # Step B: start RTT (reads the control block found above)
    echo "rtt start" \
        | nc -w 3 localhost 4444 >> /tmp/rtt_setup.log 2>/dev/null
    sleep 2

    # Step C: expose channel 0 on a TCP port so we can nc into it
    echo "rtt server start 19021 0" \
        | nc -w 3 localhost 4444 >> /tmp/rtt_setup.log 2>/dev/null
    sleep 3

    # Confirm success by checking for "rtt: Searching" or port being open
    if grep -qi "found" /tmp/rtt_setup.log || \
       netstat -tuln 2>/dev/null | grep -q ":19021"; then
        RTT_SETUP_OK=true
        break
    fi

    echo "   ↳ Not ready yet, retrying..."
    sleep 2
done

if [ "$RTT_SETUP_OK" = true ]; then
    echo "   ✓ RTT configured successfully"
else
    echo "   ⚠ RTT setup response unclear (continuing...)"
    echo "   Setup log:"
    cat /tmp/rtt_setup.log
fi

# 7. Verify RTT server is listening
echo "► Step 5: Verifying RTT server..."
sleep 2
if netstat -tuln 2>/dev/null | grep -q ":19021"; then
    echo "   ✓ RTT server listening on port 19021"
else
    echo "   ⚠ RTT port 19021 not visible yet (may still initialize)"
fi

# 8. Connect to RTT console
echo ""
echo "═════════════════════════════════════════════════════════"
echo "  RTT CONSOLE CONNECTED"
echo "  Debug output appears below. Press Ctrl+C to exit."
echo "═════════════════════════════════════════════════════════"
echo ""

if command -v nc &> /dev/null; then
    while true; do nc localhost 19021 2>/dev/null || sleep 2; done
else
    telnet localhost 19021 || true
fi

# 9. Cleanup when user exits
echo ""
echo "═════════════════════════════════════════════════════════"
echo "  Cleaning up..."
kill $OPENOCD_PID 2>/dev/null || true
killall -9 openocd 2>/dev/null || true
sleep 1
echo "  ✓ OpenOCD stopped"
echo "  ✓ Done!"
echo "═════════════════════════════════════════════════════════"