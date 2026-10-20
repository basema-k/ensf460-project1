#ifndef TIMEDELAY_H
#define TIMEDELAY_H

#include "xc.h"

// Timer1: press timing
void timer1_init(void);
void pressTimer_start(uint8_t button);
uint8_t pressTimer_stop(void);
extern volatile uint8_t longPressButton;   // nonzero = this button was held 3 s

// Timer2/3: 32-bit countdown of the set time
void countTimer_start(uint16_t totalSeconds);
void countTimer_pause(void);
void countTimer_resume(void);
uint8_t countTimer_isRunning(void);
uint16_t countTimer_remaining(void);
extern volatile uint8_t countDone;         // 1 = set time has fully elapsed

#endif
