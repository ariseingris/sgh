// ============================================================
//  gadget.cpp  —  Actuator control for STM32-A
//
//  BUG FIX: check_PhysicalButtons() now uses non-blocking
//  debounce (timestamp comparison) instead of delay(300).
//  The old delay() blocked loop() for 300 ms on every button
//  press, which stalled UART receive, sensor reads, and the
//  piston auto-stop timer.
// ============================================================

#include "gadget.h"
#include "rtt_debug.h"

extern RTTSerial rttDebug;  // defined in main.cpp

// FIX 1: activate/deactivate_system() now own the systemActive flag.
// Previously the flag was set at every call site (parseBCommand, loop keyboard,
// and physical buttons) — physical buttons were the only sites that forgot it,
// so pressing a button never updated the alert/sensor logic that reads the flag.
extern bool systemActive;

static unsigned long pistonStartMs   = 0;
static bool          pistonRunning   = false;
static const unsigned long PISTON_RUN_MS = 8000UL;

// Non-blocking debounce timestamps for each button
static unsigned long lastOpenBtnMs  = 0;
static unsigned long lastCloseBtnMs = 0;
static const unsigned long DEBOUNCE_MS = 300UL;

// ============================================================
//  setup_Actuators()
// ============================================================
void setup_Actuators() {
    pinMode(FAN_RELAY,        OUTPUT);
    pinMode(PISTON_IN1,       OUTPUT);
    pinMode(PISTON_IN2,       OUTPUT);
    pinMode(BUTTON_OPEN_PIN,  INPUT_PULLUP);
    pinMode(BUTTON_CLOSE_PIN, INPUT_PULLUP);

    digitalWrite(FAN_RELAY,  LOW);
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, LOW);
}

// ============================================================
//  Piston (H-bridge)
// ============================================================
void extend_Piston() {
    digitalWrite(PISTON_IN1, HIGH);
    digitalWrite(PISTON_IN2, LOW);
    pistonRunning = true;
    pistonStartMs = millis();
    rttDebug.println(">>> PISTON: EXTEND");
}

void retract_Piston() {
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, HIGH);
    pistonRunning = true;
    pistonStartMs = millis();
    rttDebug.println(">>> PISTON: RETRACT");
}

void stop_Piston() {
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, LOW);
    pistonRunning = false;
    rttDebug.println(">>> PISTON: STOP");
}

// ============================================================
//  Fan
// ============================================================
void turn_Fan_ON() {
    digitalWrite(FAN_RELAY, HIGH);
    rttDebug.println(">>> FAN: ON");
}

void turn_Fan_OFF() {
    digitalWrite(FAN_RELAY, LOW);
    rttDebug.println(">>> FAN: OFF");
}

// ============================================================
//  System-level shortcuts
// ============================================================
void activate_system() {
    rttDebug.println("--- SYSTEM: ACTIVATE (close) ---");
    systemActive = true;          // FIX 1: single source of truth for the flag
    turn_Fan_ON();
    retract_Piston();
}

void deactivate_system() {
    rttDebug.println("--- SYSTEM: DEACTIVATE (open) ---");
    systemActive = false;         // FIX 1: single source of truth for the flag
    turn_Fan_OFF();
    extend_Piston();
}

// ============================================================
//  update_actuators()  — call every loop()
// ============================================================
void update_actuators() {
    if (pistonRunning && (millis() - pistonStartMs >= PISTON_RUN_MS)) {
        stop_Piston();
    }
}

// ============================================================
//  check_PhysicalButtons()  — call every loop()
//
//  BUG FIX: replaced delay(300) with non-blocking debounce.
//  delay() would block loop() for 300 ms on every press,
//  preventing UART reads, sensor polling, and piston auto-stop.
// ============================================================
// ============================================================
//  check_PhysicalButtons()  — call every loop()
//
//  FIX 2: Edge-triggered detection (LOW-going edge only).
//  Previous code re-fired every DEBOUNCE_MS while the button
//  was held. Now we track the previous state; the action fires
//  only on the transition HIGH→LOW (button just pressed).
//  The DEBOUNCE_MS guard still filters contact bounce.
// ============================================================
void check_PhysicalButtons() {
    static bool prevOpen  = HIGH;
    static bool prevClose = HIGH;
    unsigned long now = millis();

    bool openNow  = digitalRead(BUTTON_OPEN_PIN);
    bool closeNow = digitalRead(BUTTON_CLOSE_PIN);

    // Falling edge (HIGH → LOW) = fresh press
    if (openNow == LOW && prevOpen == HIGH) {
        if (now - lastOpenBtnMs >= DEBOUNCE_MS) {
            lastOpenBtnMs = now;
            deactivate_system();
        }
    }
    prevOpen = openNow;

    if (closeNow == LOW && prevClose == HIGH) {
        if (now - lastCloseBtnMs >= DEBOUNCE_MS) {
            lastCloseBtnMs = now;
            activate_system();
        }
    }
    prevClose = closeNow;
}