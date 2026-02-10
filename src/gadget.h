#ifndef GADGET_H
#define GADGET_H

#include <Arduino.h>

// Định nghĩa chân cắm
#define FAN_RELAY  PA1
//
#define PISTON_IN1 PA3
#define PISTON_IN2 PA4
//
#define BUTTON_OPEN_PIN PA7
#define BUTTON_CLOSE_PIN PB0

// Khai báo các hàm
void setup_Actuators();
void open_System();
void close_System();
void extend_Piston();
void retract_Piston();
void stop_Piston();
void control_Fan(bool state);
void check_PhysicalButtons();
void toggle_StatusLED();

#endif