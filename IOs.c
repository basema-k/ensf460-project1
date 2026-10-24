/*
 * File:   IOs.c
 * Author: Basema Khan, Michelle Yoon, Vincent Fong
 *
 * Setting the time:
 *   PB1        -> seconds + 1   (held: +1 each second, +5 after 3 s)
 *   PB2        -> minutes + 1   (held: +1 each second)
 *   PB3 + PB1  -> seconds - 1   (held: -1 each second)
 *   PB3 + PB2  -> minutes - 1   (held: -1 each second)
 *   PB3 alone  -> no change
 *   All three  -> group info while held
 *   PB1 + PB2  -> short press: start the countdown
 *                 held 3 s:    reset to 00:00
 *
 * Countdown:
 *   Timer2/3 counts the whole set time (countTimer_start).
 *   Timer1 is used as a 1 s tick (pressTimer_start(0)) to print CNT.
 *   countDone -> FIN 00m : 00s - ALARM
 *
 * While counting:
 *   PB3 short press (< 3 s) -> pause, press again -> resume
 *   PB3 long press  (3 s)   -> CLR 00m : 00s, back to setting
 *
 * States: SET, RUN, PAUSE, FIN
 * PB1 = RB7 (CN23), PB2 = RA4 (CN0), PB3 = RB4 (CN1), active low
 */

#include "xc.h"
#include "IOs.h"
#include "TimeDelay.h"
#include "UART2.h"

#define PB1 1
#define PB2 2
#define PB3 4
#define START (PB1 | PB2)
#define ALL   (PB1 | PB2 | PB3)

enum { SET, RUN, PAUSE, FIN };

volatile uint8_t cnFlag = 0;

static uint8_t state   = SET;
static uint8_t buttons = 0;   // button state last time we checked
static uint8_t held    = 0;   // buttons being held (0 = none)
static uint8_t minutes = 0;
static uint8_t seconds = 0;
static uint8_t fast    = 0;   // 1 -> PB1 held 3 s, count by 5

void IOinit(void) {
    TRISBbits.TRISB7 = 1;     // buttons are inputs
    TRISAbits.TRISA4 = 1;
    TRISBbits.TRISB4 = 1;

    CNPU2bits.CN23PUE = 1;    // pull-ups
    CNPU1bits.CN0PUE  = 1;
    CNPU1bits.CN1PUE  = 1;

    CNEN2bits.CN23IE = 1;     // CN interrupt on all three buttons
    CNEN1bits.CN0IE  = 1;
    CNEN1bits.CN1IE  = 1;
    IPC4bits.CNIP = 5;
    IFS1bits.CNIF = 0;
    IEC1bits.CNIE = 1;
}

static uint8_t readButtons(void) {
    uint8_t b = 0;
    if (!PORTBbits.RB7) b |= PB1;
    if (!PORTAbits.RA4) b |= PB2;
    if (!PORTBbits.RB4) b |= PB3;
    return b;
}

void IOstart(void) {
    Disp2String("\rSET ");
    XmitUART2('0' + minutes / 10, 1);
    XmitUART2('0' + minutes % 10, 1);
    Disp2String("m : ");
    XmitUART2('0' + seconds / 10, 1);
    XmitUART2('0' + seconds % 10, 1);
    Disp2String("s               ");   // spaces wipe a longer FIN or group line
}

// One step up or down, depending on which buttons are held
static void step(void) {
    if (held == PB1) {
        if (fast && seconds % 5 == 0) seconds = (seconds + 5) % 60;
        else                          seconds = (seconds + 1) % 60;
    } else if (held == PB2) {
        minutes = (minutes + 1) % 60;
    } else if (held == (PB3 | PB1)) {
        seconds = (seconds == 0) ? 59 : seconds - 1;
    } else if (held == (PB3 | PB2)) {
        minutes = (minutes == 0) ? 59 : minutes - 1;
    } else {
        return;               // PB3 alone or other combos: no change
    }
    Disp2String("\rSET ");
    XmitUART2('0' + minutes / 10, 1);
    XmitUART2('0' + minutes % 10, 1);
    Disp2String("m : ");
    XmitUART2('0' + seconds / 10, 1);
    XmitUART2('0' + seconds % 10, 1);
    Disp2String("s               ");   // spaces wipe a longer FIN or group line
}

static void startCount(void) {
    uint16_t total = minutes * 60 + seconds;

    if (total == 0) {         // corner case: nothing to count, stay in SET
        return;
    }
    countTimer_start(total);  // Timer2/3 counts the whole time
    pressTimer_start(0);      // Timer1: 1 s ticks to update the display
    state = RUN;
    Disp2String("\rCNT ");
    XmitUART2('0' + minutes / 10, 1);
    XmitUART2('0' + minutes % 10, 1);
    Disp2String("m : ");
    XmitUART2('0' + seconds / 10, 1);
    XmitUART2('0' + seconds % 10, 1);
    Disp2String("s");
}

// A button changed
void IOcheck(void) {
    uint8_t now = readButtons();
    uint8_t added = now & ~buttons;   // buttons that just went down

    if (now == buttons) return;       // no real change
    buttons = now;

    // ----- While counting or paused: only PB3 does anything -----
    if (state == RUN || state == PAUSE) {
        if (now == 0) {                       // everything released
            uint8_t isShort = pressTimer_stop();

            if (held == PB3 && isShort && state == RUN) {
                countTimer_pause();           // short PB3: pause
                state = PAUSE;
            } else if (held == PB3 && isShort && state == PAUSE) {
                countTimer_resume();          // short PB3: resume
                pressTimer_start(0);          // 1 s ticks again
                state = RUN;
                uint16_t left = countTimer_remaining();
                minutes = left / 60;
                seconds = left % 60;
                Disp2String("\rCNT ");
                XmitUART2('0' + minutes / 10, 1);
                XmitUART2('0' + minutes % 10, 1);
                Disp2String("m : ");
                XmitUART2('0' + seconds / 10, 1);
                XmitUART2('0' + seconds % 10, 1);
                Disp2String("s");
            } else if (state == RUN) {
                pressTimer_start(0);          // keep the 1 s ticks going
            }
            held = 0;
        } else if (now == PB3 && added) {     // PB3 pressed by itself
            held = PB3;
            pressTimer_start(PB3);            // Timer1 times the press (still ticks every 1 s)
        } else {
            held = 0;                         // any other button cancels the PB3 press
        }
        return;
    }

    if (now == 0) {                   // everything released
        uint8_t isShort = pressTimer_stop();
        if (held == START && isShort) {
            startCount();
        } else if (held == ALL) {     // group info was showing: show the time again
            Disp2String("\rSET ");
            XmitUART2('0' + minutes / 10, 1);
            XmitUART2('0' + minutes % 10, 1);
            Disp2String("m : ");
            XmitUART2('0' + seconds / 10, 1);
            XmitUART2('0' + seconds % 10, 1);
            Disp2String("s               ");   // spaces wipe a longer FIN or group line
        }
        held = 0;
        return;
    }

    if (!added) {                     // one released, others still held
        if (held != START && held != ALL) {   // keep PB1+PB2 and all-three held
            pressTimer_stop();
            held = 0;
        }
        return;
    }

    // A new button went down
    if (state == FIN) {               // leave the alarm, clear the ALARM message
        state = SET;
        Disp2String("\rSET ");
        XmitUART2('0' + minutes / 10, 1);
        XmitUART2('0' + minutes % 10, 1);
        Disp2String("m : ");
        XmitUART2('0' + seconds / 10, 1);
        XmitUART2('0' + seconds % 10, 1);
        Disp2String("s               ");   // spaces wipe a longer FIN or group line
    }
    held = now;
    fast = 0;
    pressTimer_start(now);            // Timer1: 1 s ticks while held

    if (now == ALL) {                 // all three held: group info
        Disp2String("\r2026 ENSF 460 L01 - Group 7");   // 
    } else {
        step();                       // first step right away
    }
}

// Held for 3 s (Timer1 stopped itself)
void IOlong(void) {
    if ((state == RUN || state == PAUSE) && held == PB3) {   // long PB3: clear
        countTimer_pause();           // stop the countdown
        minutes = 0;
        seconds = 0;
        state = SET;
        held = 0;
        Disp2String("\rCLR 00m : 00s               ");
        return;
    }

    if (state != SET) return;

    if (held == START) {              // PB1+PB2 held: reset to 00:00
        minutes = 0;
        seconds = 0;
        Disp2String("\rSET ");
        XmitUART2('0' + minutes / 10, 1);
        XmitUART2('0' + minutes % 10, 1);
        Disp2String("m : ");
        XmitUART2('0' + seconds / 10, 1);
        XmitUART2('0' + seconds % 10, 1);
        Disp2String("s               ");   // spaces wipe a longer FIN or group line
        held = 0;
        return;
    }
    if (held == PB1) fast = 1;        // switch to +5
    if (held) pressTimer_start(held); // keep the 1 s ticks going
}

// One second passed
void IOtick(void) {
    if (state == SET) {
        step();                       // still held: another step
    } else if (state == RUN) {
        if (!T1CONbits.TON) {
            pressTimer_start(0);      // Timer1 stops itself after 3 s; restart it
        }
        uint16_t left = countTimer_remaining();
        if (left == 0) return;        // the end is handled by IOdone()
        minutes = left / 60;
        seconds = left % 60;
        Disp2String("\rCNT ");
        XmitUART2('0' + minutes / 10, 1);
        XmitUART2('0' + minutes % 10, 1);
        Disp2String("m : ");
        XmitUART2('0' + seconds / 10, 1);
        XmitUART2('0' + seconds % 10, 1);
        Disp2String("s");
    }
}

// Timer2/3 reached the set time
void IOdone(void) {
    countDone = 0;
    if (state != RUN) return;

    pressTimer_stop();                // no more ticks needed
    held = 0;                         // a button still held: ignore its release
    state = FIN;
    minutes = 0;
    seconds = 0;
    Disp2String("\rFIN ");
    XmitUART2('0' + minutes / 10, 1);
    XmitUART2('0' + minutes % 10, 1);
    Disp2String("m : ");
    XmitUART2('0' + seconds / 10, 1);
    XmitUART2('0' + seconds % 10, 1);
    Disp2String("s - ALARM");
}

// CN ISR: only sets a flag
void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void) {
    IFS1bits.CNIF = 0;
    cnFlag = 1;
}
