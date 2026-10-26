/*
 * File:   IOs.h
 * Author: Basema Khan, Michelle Yoon, Vincent Fong
 */

#ifndef IOS_H
#define IOS_H

#include <xc.h>
#include <stdint.h>

extern volatile uint8_t cnFlag;   // CN ISR: a button changed

void IOinit(void);
void IOstart(void);
void IOcheck(void);   // a button changed
void IOlong(void);    // longPressButton was set (held 3 s)
void IOtick(void);    // Timer1 one-second tick
void IOdone(void);    // countDone was set

uint8_t IOisFinished(void);
uint8_t IOisSetting(void);

#endif
