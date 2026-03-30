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
    Serial.println(">>> PISTON: EXTEND");
}

void retract_Piston() {
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, HIGH);
    pistonRunning = true;
    pistonStartMs = millis();
    Serial.println(">>> PISTON: RETRACT");
}

void stop_Piston() {
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, LOW);
    pistonRunning = false;
    Serial.println(">>> PISTON: STOP");
}

// ============================================================
//  Fan
// ============================================================
void turn_Fan_ON() {
    digitalWrite(FAN_RELAY, HIGH);
    Serial.println(">>> FAN: ON");
}

void turn_Fan_OFF() {
    digitalWrite(FAN_RELAY, LOW);
    Serial.println(">>> FAN: OFF");
}

// ============================================================
//  System-level shortcuts
// ============================================================
void activate_system() {
    Serial.println("--- SYSTEM: ACTIVATE (close) ---");
    turn_Fan_ON();
    retract_Piston();
}

void deactivate_system() {
    Serial.println("--- SYSTEM: DEACTIVATE (open) ---");
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
void check_PhysicalButtons() {
    unsigned long now = millis();

    if (digitalRead(BUTTON_OPEN_PIN) == LOW) {
        if (now - lastOpenBtnMs >= DEBOUNCE_MS) {
            lastOpenBtnMs = now;
            deactivate_system();
        }
    }

    if (digitalRead(BUTTON_CLOSE_PIN) == LOW) {
        if (now - lastCloseBtnMs >= DEBOUNCE_MS) {
            lastCloseBtnMs = now;
            activate_system();
        }
    }
}