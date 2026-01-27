#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <Arduino.h>

// Định nghĩa chân cắm
#define FAN_RELAY_PIN PA1  // Chân nối Relay điều khiển quạt
#define STATUS_LED_PIN PC13 // Đèn LED trên mạch Blue Pill

void setup_Actuators();
void control_Fan(bool state);
void toggle_StatusLED();

#endif