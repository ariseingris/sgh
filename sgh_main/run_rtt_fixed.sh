#!/bin/bash
# ============================================================
# STM32-A Sensor Node | RTT Debug Console
# Usage: ./run_rtt_stm32a.sh
# Requires: ST-Link connected to STM32-A SWD pins
#   SWDIO → PA13 | SWCLK → PA14 | GND → GND | 3.3V → 3.3V
# ============================================================

set -e  # exit on unexpected error (removed for individual step control below)
set +e

TELNET_PORT=4444
RTT_PORT=19021
OPENOCD_LOG=/tmp/openocd_stm32a.log
RTT_SETUP_LOG=/tmp/rtt_setup_stm32a.log

echo "═════════════════════════════════════════════════════════"
echo "  STM32-A Sensor Node | RTT Debugger"
echo "═════════════════════════════════════════════════════════"

# ─────────────────────────────────────────────────────────────
# BUG-8 FIX: trap ensures OpenOCD is killed on Ctrl+C or error.
# Without this, killing the nc session left OpenOCD running as
# a zombie, causing "address already in use" on the next run.
# ─────────────────────────────────────────────────────────────
OPENOCD_PID=""
cleanup() {
    echo ""
    echo "► Cleaning up..."
    [ -n "$OPENOCD_PID" ] && kill "$OPENOCD_PID" 2>/dev/null || true
    killall -q openocd 2>/dev/null || true
    echo "   ✓ Done"
}
trap cleanup INT TERM EXIT

# ─────────────────────────────────────────────────────────────
# Step 1: Find OpenOCD
# ─────────────────────────────────────────────────────────────
echo "► Step 1: Locating OpenOCD..."
OPENOCD_BIN=$(find "$HOME/.platformio/packages/tool-openocd" -name "openocd" -type f 2>/dev/null | head -n 1)
if [ -z "$OPENOCD_BIN" ]; then
    echo "❌ OpenOCD not found. Run: pio run -t upload (triggers package install)"
    exit 1
fi
echo "   ✓ $OPENOCD_BIN"

# ─────────────────────────────────────────────────────────────
# Step 2: Kill any stale OpenOCD + fix USB permissions
# ─────────────────────────────────────────────────────────────
echo "► Step 2: Cleaning up stale processes..."
killall -q -9 openocd 2>/dev/null || true
sleep 1
sudo chmod -R 777 /dev/bus/usb/ 2>/dev/null || true

# ─────────────────────────────────────────────────────────────
# Step 3: Start OpenOCD
#
# BUG-1 FIX: added -c "init" so OpenOCD actually attaches to the
# target. Without "init", the telnet interface opens but all RTT
# and debug commands fail silently because no target is connected.
#
# NOTE: No "reset halt" here — we want the firmware to keep
# running so RTT data flows. "init" alone attaches non-intrusively.
# ─────────────────────────────────────────────────────────────
echo "► Step 3: Starting OpenOCD (ST-Link → STM32-A)..."
"$OPENOCD_BIN" \
    -f interface/stlink.cfg \
    -f target/stm32f1x.cfg \
    -c "telnet_port $TELNET_PORT" \
    -c "gdb_port disabled" \
    -c "tcl_port disabled" \
    -c "init" \
    > "$OPENOCD_LOG" 2>&1 &
OPENOCD_PID=$!

# Wait up to 5s for OpenOCD to fully start
for i in $(seq 1 10); do
    sleep 0.5
    if ! ps -p "$OPENOCD_PID" > /dev/null 2>&1; then
        echo "❌ OpenOCD crashed. Log:"
        cat "$OPENOCD_LOG"
        echo ""
        echo "Checklist:"
        echo "  • ST-Link physically connected to STM32-A (not STM32-B)"
        echo "  • BOOT0 jumper = GND (0)"
        echo "  • lsusb shows: ID 0483:3748 ST-LINK/V2"
        exit 1
    fi
    # OpenOCD is ready when telnet port is open
    if ss -tuln 2>/dev/null | grep -q ":$TELNET_PORT" || \
       netstat -tuln 2>/dev/null | grep -q ":$TELNET_PORT"; then
        break
    fi
done

if ! ps -p "$OPENOCD_PID" > /dev/null 2>&1; then
    echo "❌ OpenOCD failed. Log:"; cat "$OPENOCD_LOG"; exit 1
fi
echo "   ✓ OpenOCD running (PID: $OPENOCD_PID)"

# ─────────────────────────────────────────────────────────────
# Step 4: Configure RTT via telnet
#
# BUG-2 FIX: rtt setup search range covers full 20 KB SRAM of
# STM32F103C8 (0x20000000 + 0x5000). The ID string "SEGGER RTT"
# must match exactly what SEGGER_RTT_Init() writes into RAM.
#
# BUG-3 FIX: generous sleeps between commands so OpenOCD has time
# to process each one before the next arrives. rtt setup scans RAM
# which can take ~200ms; rtt start needs setup to succeed first.
#
# BUG-7 FIX: "resume" issued before rtt start so the CPU is
# running and actually producing RTT data. If the chip was left
# halted from a previous session, RTT output would be silent.
# ─────────────────────────────────────────────────────────────
echo "► Step 4: Configuring RTT channel..."

RTT_OK=0
for attempt in 1 2 3; do
    echo "   Attempt $attempt/3..."
    {
        sleep 0.5
        # Make sure CPU is running — firmware must execute to fill RTT buffer
        echo "resume"
        sleep 1
        # Search all 20 KB of SRAM for the RTT control block
        echo "rtt setup 0x20000000 0x5000 {SEGGER RTT}"
        sleep 1
        echo "rtt start"
        sleep 1
        echo "rtt server start $RTT_PORT 0"
        sleep 0.5
        echo "exit"
    } | nc -w 5 localhost "$TELNET_PORT" > "$RTT_SETUP_LOG" 2>&1

    # BUG-9 FIX: check the telnet response log for success/failure
    if grep -q "rtt server started" "$RTT_SETUP_LOG" 2>/dev/null || \
       grep -q "Listening on port $RTT_PORT" "$RTT_SETUP_LOG" 2>/dev/null; then
        RTT_OK=1
        break
    fi
    echo "   RTT setup response:"
    cat "$RTT_SETUP_LOG" | sed 's/^/     /'
    sleep 1
done

if [ "$RTT_OK" -eq 0 ]; then
    echo ""
    echo "⚠ RTT server may not have confirmed — checking port anyway..."
    echo "  (If you see output below, RTT is working despite no confirmation)"
fi

# ─────────────────────────────────────────────────────────────
# Step 5: Wait for RTT port 19021 to open
#
# BUG-5 FIX: if the port never opens after 10 seconds, print the
# OpenOCD log and exit with a clear error instead of silently
# connecting nc to a port that isn't serving RTT data.
# ─────────────────────────────────────────────────────────────
echo "► Step 5: Waiting for RTT server on port $RTT_PORT..."
PORT_OPEN=0
for i in $(seq 1 15); do
    if ss -tuln 2>/dev/null | grep -q ":$RTT_PORT" || \
       netstat -tuln 2>/dev/null | grep -q ":$RTT_PORT"; then
        PORT_OPEN=1
        echo "   ✓ RTT server ready on port $RTT_PORT"
        break
    fi
    sleep 1
    echo "   Waiting... ($i/15)"
done

if [ "$PORT_OPEN" -eq 0 ]; then
    echo "❌ RTT port $RTT_PORT never opened."
    echo ""
    echo "OpenOCD log:"
    cat "$OPENOCD_LOG" | sed 's/^/  /'
    echo ""
    echo "RTT setup log:"
    cat "$RTT_SETUP_LOG" | sed 's/^/  /'
    echo ""
    echo "Common causes:"
    echo "  • Firmware not flashed yet (flash first with: pio run -t upload)"
    echo "  • SEGGER_RTT_Init() not called in setup() before loop()"
    echo "  • RTT control block not found in SRAM — try power-cycling STM32-A"
    exit 1
fi

# ─────────────────────────────────────────────────────────────
# Step 6: Connect RTT console
#
# BUG-6 FIX: wrapped nc in a reconnect loop. Plain `nc` exits
# immediately when OpenOCD closes the TCP connection (e.g. on
# a glitch). The loop re-connects automatically so you don't
# lose the console unexpectedly.
# ─────────────────────────────────────────────────────────────
echo ""
echo "═════════════════════════════════════════════════════════"
echo "  STM32-A RTT CONSOLE — Ctrl+C to exit"
echo "  Watching for: I2C scan, sensor init, DATA:, watchdog"
echo "═════════════════════════════════════════════════════════"
echo ""

# Reconnect loop — if nc drops, wait 1s and reconnect
while ps -p "$OPENOCD_PID" > /dev/null 2>&1; do
    nc localhost "$RTT_PORT" || true
    sleep 1
    # Only reconnect if OpenOCD is still alive
    if ps -p "$OPENOCD_PID" > /dev/null 2>&1; then
        echo "   [RTT console disconnected — reconnecting...]"
    else
        echo "   [OpenOCD stopped — exiting]"
        break
    fi
done

# cleanup() called automatically by trap on EXIT