#ifndef GADGET_H
#define GADGET_H

#include <Arduino.h>
#include "rtt_debug.h"

// -------------------------------------------------------
//  Pin definitions  (STM32-A Blue Pill)
// -------------------------------------------------------
#define FAN_RELAY        PA1
#define PISTON_IN1       PA3
#define PISTON_IN2       PA4
#define BUTTON_OPEN_PIN  PA7
#define BUTTON_CLOSE_PIN PB0

// -------------------------------------------------------
//  API
// -------------------------------------------------------
void setup_Actuators();

void extend_Piston();
void retract_Piston();
void stop_Piston();

void turn_Fan_ON();
void turn_Fan_OFF();

void activate_system();
void deactivate_system();

void update_actuators();
void check_PhysicalButtons();

#endif // GADGET_H