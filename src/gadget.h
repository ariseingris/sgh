#ifndef GADGET_H
#define GADGET_H

#include <Arduino.h>

// Định nghĩa chân cắm
#define FAN_RELAY  PA1
#define PISTON_IN1 PA3
#define PISTON_IN2 PA4
#define STATUS_LED_PIN PC13
#define BUTTON_OPEN_PIN PA7
#define BUTTON_CLOSE_PIN PB0

// Khai báo các hàm
void setup_Actuators();
void control_Piston(int mode);
void control_Fan(bool state);
void check_PhysicalButtons();
void toggle_StatusLED();

#endif