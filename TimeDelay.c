/*
 * File:   TimeDelay.c
 * Author: Basema Khan, Michelle Yoon, Vincent Fong
 */

#include "xc.h"
#include "TimeDelay.h"

// Fcy = 250 kHz (newClk(500)), 1:8 prescaler on all timers
// 31250 ticks per second
#define TICKS_PER_SEC 31250

static volatile uint8_t seconds     = 0;   // Timer1 one-second ticks
static volatile uint8_t timedButton = 0;   // button currently being timed
static uint32_t countPeriod = 0;           // total ticks loaded into PR3:PR2

volatile uint8_t longPressButton = 0;      // read and cleared by IOs.c
volatile uint8_t countDone = 0;            // read and cleared by IOs.c

// Timer1: deciding if each press is long or short

void timer1_init(void) {
    T1CONbits.TON   = 0;
    T1CONbits.TCS   = 0;      // Fcy
    T1CONbits.TCKPS = 1;      // 1:8
    TMR1 = 0;
    PR1  = TICKS_PER_SEC;     // 1 sec

    IPC0bits.T1IP = 4;
    IFS0bits.T1IF = 0;
    IEC0bits.T1IE = 1;
}

// Called from the CN ISR when a button goes down
void pressTimer_start(uint8_t button) {
    timedButton = button;
    seconds = 0;
    TMR1 = 0;
    IFS0bits.T1IF = 0;
    T1CONbits.TON = 1;
}

// Called from the CN ISR when the button is released.
// Returns 1 if still running (short press), 0 if long press already reported.
uint8_t pressTimer_stop(void) {
    uint8_t wasRunning = T1CONbits.TON;
    T1CONbits.TON = 0;
    return wasRunning;
}

void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void) {
    IFS0bits.T1IF = 0;
    seconds++;

    if (seconds >= 3) {            // held 3 s -> long press
        T1CONbits.TON = 0;
        longPressButton = timedButton;
    }
}

// Timer2/3: 32-bit countdown of the set time

// Load the set time and start counting from zero
void countTimer_start(uint16_t totalSeconds) {
    if (totalSeconds == 0) {
        return;                    // nothing to count
    }

    countPeriod = (uint32_t)totalSeconds * TICKS_PER_SEC;

    T2CONbits.TON   = 0;
    T2CONbits.T32   = 1;           // combine Timer2 + Timer3
    T2CONbits.TCS   = 0;           // Fcy
    T2CONbits.TCKPS = 1;           // 1:8

    TMR3HLD = 0;                   // high word first
    TMR2    = 0;                   // then low word
    PR3 = (uint16_t)(countPeriod >> 16);
    PR2 = (uint16_t)countPeriod;

    IPC2bits.T3IP = 3;             // 32-bit match interrupt comes from Timer3
    IFS0bits.T3IF = 0;
    IEC0bits.T3IE = 1;

    countDone = 0;
    T2CONbits.TON = 1;
}

// Stop counting but keep the current count
void countTimer_pause(void) {
    T2CONbits.TON = 0;
}

// Continue from where it was paused
void countTimer_resume(void) {
    if (!countDone && countPeriod != 0) {
        T2CONbits.TON = 1;
    }
}

uint8_t countTimer_isRunning(void) {
    return T2CONbits.TON;
}

// Seconds left, rounded up so it shows 1 until the time actually ends
uint16_t countTimer_remaining(void) {
    if (countDone || countPeriod == 0) {
        return 0;
    }

    uint16_t lo = TMR2;            // reading TMR2 latches TMR3 into TMR3HLD
    uint16_t hi = TMR3HLD;
    uint32_t elapsed = ((uint32_t)hi << 16) | lo;

    if (elapsed >= countPeriod) {
        return 0;
    }

    uint32_t left = countPeriod - elapsed;
    return (uint16_t)((left + TICKS_PER_SEC - 1) / TICKS_PER_SEC);
}

void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void) {
    IFS0bits.T3IF = 0;
    T2CONbits.TON = 0;             // stop at the end
    countDone = 1;                 // set time has elapsed
}
