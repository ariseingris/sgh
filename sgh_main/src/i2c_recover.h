// ============================================================
//  i2c_recover.h  —  I2C bus recovery for STM32-A
//
//  When a sensor holds SDA low (mid-transaction crash), the
//  entire I2C bus locks up. Wire.begin() alone won't fix it.
//  This clocks SCL manually until SDA is released, then
//  reinitialises the Wire peripheral.
//
//  Call recoverI2C() any time sensors start returning bad values.
// ============================================================
#ifndef I2C_RECOVER_H
#define I2C_RECOVER_H

#include <Arduino.h>
#include <Wire.h>

// Blue Pill default I2C pins
#define I2C_SDA_PIN  PB7
#define I2C_SCL_PIN  PB6

// ============================================================
//  recoverI2C()
//  Clocks SCL up to 9 times until SDA goes HIGH (released).
//  Then sends a STOP condition and re-inits Wire at 400 kHz.
//  Returns true if bus recovered, false if SDA still stuck.
// ============================================================
inline bool recoverI2C() {
    // Switch pins to GPIO mode temporarily
    pinMode(I2C_SDA_PIN, INPUT_PULLUP);
    pinMode(I2C_SCL_PIN, OUTPUT);

    bool recovered = false;
    for (int i = 0; i < 9; i++) {
        digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
        digitalWrite(I2C_SCL_PIN, LOW);  delayMicroseconds(5);
        if (digitalRead(I2C_SDA_PIN) == HIGH) {
            recovered = true;
            break;
        }
    }

    // Send STOP condition: SDA LOW→HIGH while SCL HIGH
    pinMode(I2C_SDA_PIN, OUTPUT);
    digitalWrite(I2C_SDA_PIN, LOW);  delayMicroseconds(5);
    digitalWrite(I2C_SCL_PIN, HIGH); delayMicroseconds(5);
    digitalWrite(I2C_SDA_PIN, HIGH); delayMicroseconds(5);

    // Hand pins back to Wire peripheral
    Wire.end();
    delay(10);
    Wire.begin();
    Wire.setClock(400000);

    return recovered;
}

// ============================================================
//  scanI2C()
//  Returns number of responding devices. 0 = bus still dead.
// ============================================================
inline int scanI2C() {
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) found++;
    }
    return found;
}

#endif // I2C_RECOVER_H