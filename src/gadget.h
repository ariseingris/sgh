#ifndef GADGET_H
#define GADGET_H

#include <Arduino.h>

// Định nghĩa chân cắm
#define FAN_RELAY_PIN  PA4 
#define PISTON_IN1_PIN PA5
#define PISTON_IN2_PIN PA6
#define STATUS_LED_PIN PC13

// Khai báo các hàm
void setup_Actuators();
void control_Piston(int mode);
void control_Fan(bool state);
void toggle_StatusLED();

#endif