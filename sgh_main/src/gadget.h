#ifndef GADGET_H
#define GADGET_H

#include <Arduino.h>
#include "rtt_debug.h"

// -------------------------------------------------------
//  Pin definitions  (STM32-A Blue Pill)
// -------------------------------------------------------
#define FAN_RELAY         PA1
#define PISTON_IN1        PA3
#define PISTON_IN2        PA4
#define BUTTON_OPEN_PIN   PA7
#define BUTTON_CLOSE_PIN  PB0
#define BUTTON_TOGGLE_PIN PB12   // IDLE↔MEASURING toggle

// -------------------------------------------------------
//  State machine
// -------------------------------------------------------
enum SystemState { STATE_IDLE, STATE_MOVING, STATE_MEASURING };
SystemState getCurrentState();
bool        isMeasuring();

void enterMeasuring();   // called by parseBCommand and RTT key '1'
void enterIdle();        // called by parseBCommand and RTT key '2'

// -------------------------------------------------------
//  Piston direction tracker
// -------------------------------------------------------
enum PistonDir { PISTON_STOPPED, PISTON_EXTENDING, PISTON_RETRACTING };
PistonDir getPistonDir();

// -------------------------------------------------------
//  API
// -------------------------------------------------------
void setup_Actuators();
void update_actuators();   // call every loop() — piston auto-stop + state transitions

void extend_Piston();
void retract_Piston();
void stop_Piston();

void turn_Fan_ON();
void turn_Fan_OFF();

void check_PhysicalButtons();

bool getFanState();     // true = fan ON (FAN_RELAY HIGH)
bool getPistonState();  // true = piston retracted/closed (pistonDir-based)

#endif // GADGET_H
