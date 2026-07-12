#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <Arduino.h>

// Function declarations
void statusLEDCheck();
void processCommands(byte hp, byte typeState);
void varResets(byte i);
void flushCommandArray(byte i, byte type);
void positionHP(byte hp, byte pos, int speed);
void ledOFF(byte hp);
void resetLEDtwitch(int hp);
void colorProjectorLED(byte hp, int c);
void dimPulse(byte hp, int c, int setting);
void cycle(byte hp, int c);
void ledColor(byte hp, int c);
void rainbow(byte hp);
void ShortCircuit(byte hp, int c);
void twitchHP(byte hp, byte randtwitch);
void resetHPtwitch(byte hp);
void wagHP(byte hp, byte type);
void RCHP(byte hp, byte type);
void enableServos();
void statusLEDOn();
void serialEvent();
void i2cEvent(int howMany);

// Utility functions
uint32_t Wheel(byte WheelPos);
uint32_t dimColorVal(int c, int brightness);
uint32_t Color(byte r, byte g, byte b);
int mapPulselength(double microseconds);

#endif 