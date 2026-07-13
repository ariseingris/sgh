// ============================================================
//  gadget.cpp  —  Actuator control for STM32-A
//
//  BUG FIX: check_PhysicalButtons() uses non-blocking debounce
//  (timestamp comparison) instead of delay(300).
//  State machine (STATE_IDLE/MOVING/MEASURING) replaces old
//  systemActive boolean. update_actuators() drives the
//  MOVING → MEASURING/IDLE transition after piston auto-stop.
// ============================================================

#include "gadget.h"
#include "rtt_debug.h"

extern RTTSerial rttDebug;  // defined in main.cpp

// -------------------------------------------------------
//  State machine
// -------------------------------------------------------
static SystemState    currentState    = STATE_IDLE;
static PistonDir      pistonDir       = PISTON_STOPPED;
static unsigned long  stateEnteredMs  = 0;
static bool pistonClosed = false; // giữ nguyên sau khi dừng


// -------------------------------------------------------
//  Piston auto-stop timer
// -------------------------------------------------------
static unsigned long pistonStartMs   = 0;
static bool          pistonRunning   = false;
static const unsigned long PISTON_RUN_MS = 8000UL;

// Non-blocking debounce timestamps
static unsigned long lastOpenBtnMs  = 0;
static unsigned long lastCloseBtnMs = 0;
static unsigned long lastToggleMs   = 0;
static const unsigned long DEBOUNCE_MS = 300UL;

// ============================================================
//  setup_Actuators()
// ============================================================
void setup_Actuators() {
    pinMode(FAN_RELAY,         OUTPUT);
    pinMode(PISTON_IN1,        OUTPUT);
    pinMode(PISTON_IN2,        OUTPUT);
    pinMode(BUTTON_OPEN_PIN,   INPUT_PULLUP);
    pinMode(BUTTON_CLOSE_PIN,  INPUT_PULLUP);
    pinMode(BUTTON_TOGGLE_PIN, INPUT_PULLUP);

    digitalWrite(FAN_RELAY,  LOW);
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, LOW);
}

// ============================================================
//  Piston (H-bridge)
// ============================================================
void extend_Piston() {
    if (currentState != STATE_MOVING) {
        currentState   = STATE_MOVING;
        stateEnteredMs = millis();
        rttDebug.println("[STATE] Manual piston cmd → MOVING");
    }
    digitalWrite(PISTON_IN1, HIGH);
    digitalWrite(PISTON_IN2, LOW);
    pistonRunning = true;
    pistonStartMs = millis();
    pistonDir     = PISTON_EXTENDING;
    rttDebug.println(">>> PISTON: EXTEND");
}

void retract_Piston() {
    if (currentState != STATE_MOVING) {
        currentState   = STATE_MOVING;
        stateEnteredMs = millis();
        rttDebug.println("[STATE] Manual piston cmd → MOVING");
    }
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, HIGH);
    pistonRunning = true;
    pistonStartMs = millis();
    pistonDir     = PISTON_RETRACTING;
    rttDebug.println(">>> PISTON: RETRACT");
}


void stop_Piston() {
    if (pistonDir == PISTON_RETRACTING) pistonClosed = true;
    else if (pistonDir == PISTON_EXTENDING) pistonClosed = false;
    digitalWrite(PISTON_IN1, LOW);
    digitalWrite(PISTON_IN2, LOW);
    pistonRunning = false;
    pistonDir = PISTON_STOPPED;
}
bool getPistonState() { return pistonClosed; }

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
//  State machine
// ============================================================
SystemState getCurrentState() {
    return currentState;
}

bool isMeasuring() {
    return currentState == STATE_MEASURING;
}

PistonDir getPistonDir() {
    return pistonDir;
}

void enterMeasuring() {
    rttDebug.println("[STATE] → MOVING (piston retracting toward MEASURING)");
    currentState   = STATE_MOVING;
    stateEnteredMs = millis();
    turn_Fan_ON();
    retract_Piston();
}

void enterIdle() {
    rttDebug.println("[STATE] → MOVING (piston extending toward IDLE)");
    currentState   = STATE_MOVING;
    stateEnteredMs = millis();
    turn_Fan_OFF();
    extend_Piston();
}

// ============================================================
//  update_actuators()  — call every loop()
//
//  CLAUDE.md rule 5: piston auto-stop is non-negotiable.
//  Also drives the MOVING → MEASURING/IDLE state transition
//  once the piston finishes its run.
//  Safety timeout: if piston has been running > PISTON_RUN_MS + 2s,
//  force-stop regardless (guards against auto-stop being bypassed).
// ============================================================
void update_actuators() {
    unsigned long now = millis();

    if (pistonRunning && (now - pistonStartMs >= PISTON_RUN_MS)) {
        stop_Piston();

        if (currentState == STATE_MOVING) {
            if (getFanState()) {
                currentState = STATE_MEASURING;
                rttDebug.println("[STATE] MOVING → MEASURING");
            } else {
                currentState = STATE_IDLE;
                rttDebug.println("[STATE] MOVING → IDLE");
            }
        }
    }

    // Safety: force-stop if stuck in MOVING beyond PISTON_RUN_MS + 2 s
    if (currentState == STATE_MOVING &&
        (now - stateEnteredMs) > (PISTON_RUN_MS + 2000UL)) {
        stop_Piston();
        if (getFanState()) {
            currentState = STATE_MEASURING;
            rttDebug.println("[STATE] TIMEOUT: MOVING → MEASURING");
        } else {
            currentState = STATE_IDLE;
            rttDebug.println("[STATE] TIMEOUT: MOVING → IDLE");
        }
    }
}

// ============================================================
//  check_PhysicalButtons()  — call every loop()
//
//  FIX: edge-triggered detection (LOW-going edge only).
//  FIX: toggle button now uses enterMeasuring()/enterIdle()
//       instead of the removed activate/deactivate_system().
// ============================================================
void check_PhysicalButtons() {
    static bool prevOpen   = HIGH;
    static bool prevClose  = HIGH;
    static bool prevToggle = HIGH;
    unsigned long now = millis();

    bool openNow   = digitalRead(BUTTON_OPEN_PIN);
    bool closeNow  = digitalRead(BUTTON_CLOSE_PIN);
    bool toggleNow = digitalRead(BUTTON_TOGGLE_PIN);

    // PA7: extend piston only
    if (openNow == LOW && prevOpen == HIGH) {
        if (now - lastOpenBtnMs >= DEBOUNCE_MS) {
            lastOpenBtnMs = now;
            extend_Piston();
        }
    }
    prevOpen = openNow;

    // PB0: retract piston only
    if (closeNow == LOW && prevClose == HIGH) {
        if (now - lastCloseBtnMs >= DEBOUNCE_MS) {
            lastCloseBtnMs = now;
            retract_Piston();
        }
    }
    prevClose = closeNow;

    // PB12: toggle IDLE ↔ MEASURING via state machine
    if (toggleNow == LOW && prevToggle == HIGH) {
        if (now - lastToggleMs >= DEBOUNCE_MS) {
            lastToggleMs = now;
            if (isMeasuring()) enterIdle();
            else               enterMeasuring();
        }
    }
    prevToggle = toggleNow;
}

// ============================================================
//  State getters — for DATA: telemetry payload
// ============================================================
bool getFanState() {
    return digitalRead(FAN_RELAY) == HIGH;
}

bool getPistonState() {
    // true = piston retracted/closed (matches old semantics)
    return pistonDir == PISTON_RETRACTING;
}
