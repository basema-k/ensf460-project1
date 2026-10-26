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
 * PB1 = RB7 (CN23), PB2 = RB4 (CN0), PB3 = RA4 (CN1), active low
 */

#include "xc.h"
#include "IOs.h"
#include "TimeDelay.h"
#include "UART2.h"

#define PB1 1                   // 001
#define PB2 2                   // 010
#define PB3 4                   // 100 (4 is used instead of 3 so it isn't the same as PB1 | PB2)
#define START (PB1 | PB2)       // 011 (PB1 + PB2)
#define ALL   (PB1 | PB2 | PB3) // 111 (All 3 buttons)

// Operating states of the countdown timer.
enum { SET, RUN, PAUSE, FIN };  // SET = 0, RUN = 1, PAUSE = 2, FIN = 3

volatile uint8_t cnFlag = 0;    // Set by CN ISR when a button change needs processing

static uint8_t state   = SET;   // Current operating state
static uint8_t buttons = 0;     // Previous physical button combination
static uint8_t held    = 0;     // Button combination currently being handled
static uint8_t minutes = 0;     // Displayed minutes (0-59)
static uint8_t seconds = 0;     // Displayed seconds (0-59)
static uint8_t fast    = 0;     // PB1 fast-increment mode after a long hold
static uint8_t wasLong = 0;     // START long press already handled

void IOinit(void) {
    TRISBbits.TRISB7 = 1;       // Configures PB1 as an input
    TRISBbits.TRISB4 = 1;       // Configures PB2 as an input
    TRISAbits.TRISA4 = 1;       // Configures PB3 as an input
    
    TRISBbits.TRISB9 = 0;       // Configures LED1 as an output
    TRISAbits.TRISA6 = 0;       // Configures LED2 as an output
    
    LATBbits.LATB9 = 0;         // LED1 starts OFF
    LATAbits.LATA6 = 0;         // LED2 starts OFF

    CNPU2bits.CN23PUE = 1;      // Enable internal pull-up resistor for PB1
    CNPU1bits.CN1PUE  = 1;      // Enable internal pull-up resistor for PB2
    CNPU1bits.CN0PUE  = 1;      // Enable internal pull-up resistor for PB3

    CNEN2bits.CN23IE = 1;       // Enable CN interrupts for PB1
    CNEN1bits.CN1IE  = 1;       // Enable CN interrupts for PB2
    CNEN1bits.CN0IE  = 1;       // Enable CN interrupts for PB3
    
    IPC4bits.CNIP = 5;          // Sets the CN interrupt priority to 5
    IFS1bits.CNIF = 0;          // Clear CN interrupt flag
    IEC1bits.CNIE = 1;          // Enable CN interrupt
}


// Read active-low button pins and return a combined bit mask
static uint8_t readButtons(void) {
    uint8_t b = 0;              // Start with no buttons pressed
    
    // A LOW input means the corresponding button is pressed (active-low)
    // If a button is pressed it is 0, so using ! makes the if() true when a button is pressed
    if (!PORTBbits.RB7) b |= PB1;   
    if (!PORTBbits.RB4) b |= PB2;
    if (!PORTAbits.RA4) b |= PB3;
    return b;                   // 000-111 or 0-7 represents the current button combination
}

// Return 1 if the program is in SET mode, otherwise 0
uint8_t IOisSetting(void){
    return state == SET;
}

// Display the current time in SET mode on the UART terminal
void IOstart(void) {
    Disp2String("\rSET ");               // Return to start of line and print SET
    
    //Print minutes as two ASCII digits
    XmitUART2('0' + minutes / 10, 1);    // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
    XmitUART2('0' + minutes % 10, 1);    // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)
    
    Disp2String("m : ");
    
    //Print seconds as two ASCII digits
    XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
    XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
    Disp2String("s               ");    // The spaces erase the leftover characters from longer messages
}

// One step up or down, depending on which buttons are held
static void step(void) {
    // Increase minutes, and wrap around after 59
    if (held == PB1) {
        
        // After a 3-second hold, increase by 5 at multiples of 5
        // Otherwise increase by 1
        if (fast && seconds % 5 == 0) seconds = (seconds + 5) % 60;
        else                          seconds = (seconds + 1) % 60;
        
    // Increase minutes, and wrap around after 59
    } else if (held == PB2) {
        minutes = (minutes + 1) % 60;
        
    // PB1 + PB3 decreases seconds, wrapping 0 back to 59
    } else if (held == (PB3 | PB1)) {
        seconds = (seconds == 0) ? 59 : seconds - 1;
        
    // PB2 + PB3 decreases seconds, wrapping 0 back to 59
    } else if (held == (PB3 | PB2)) {
        minutes = (minutes == 0) ? 59 : minutes - 1;
        
    // Other combinations do not change time 
    } else {
        return;               
    }
    
    // Display the current time in SET mode on the UART terminal
    Disp2String("\rSET ");              // Return to start of line and print SET
    
    //Print minutes as two ASCII digits
    XmitUART2('0' + minutes / 10, 1);   // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
    XmitUART2('0' + minutes % 10, 1);   // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)
    
    Disp2String("m : ");
    
    //Print seconds as two ASCII digits
    XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
    XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
    Disp2String("s               ");    // The spaces erase the leftover characters from longer messages
}

static void startCount(void) {
    uint16_t total = minutes * 60 + seconds;    // Convert minutes and seconds into total seconds (16-bit int used since 59 * 60 + 59 = 3599 seconds which is greater than 255 so an 8-bit int is not large enough)

    // Do not start a countdown if the selected time is zero
    if (total == 0) {         
        return;
    }
    
    countTimer_start(total);        // Timer 2/3 measures the full countdown duration
    pressTimer_start(0);            // Timer 1 generates 1 second ticks, the 0 argument disables long-press detection
    state = RUN;                    // Enter countdown mode
    LATBbits.LATB9 = 1;             // Start LED1 ON, Timer 1 will toggle it each second
    LATAbits.LATA6 = 0;             // Keep alarm LED2 OFF during countdown
    
    // Display the current time in RUN mode on the UART terminal
    Disp2String("\rCNT ");              // Return to start of line and print CNT
    
    //Print minutes as two ASCII digits
    XmitUART2('0' + minutes / 10, 1);    // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
    XmitUART2('0' + minutes % 10, 1);    // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)
    
    Disp2String("m : ");
    
    //Print seconds as two ASCII digits
    XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
    XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
    
    Disp2String("s");
}

// Process button changes by comparing current and previous button states
void IOcheck(void) {
    uint8_t now = readButtons();        // Bit mask of the buttons currently pressed
    uint8_t added = now & ~buttons;     // Identify newly pressed buttons by comparing current and previous states

    if (now == buttons) return;         // Ignore if the button state has not changed
    buttons = now;                      // Save current state for the next comparison

    
    // RUN/PAUSE: PB3 short press pauses/resumes, long press clears countdown
    if (state == RUN || state == PAUSE) {
        if (now == 0) {                             // No button is physically pressed
            uint8_t isShort = pressTimer_stop();    // Stop Timer 1 and check if it was still running, still running means there was not a long press, so it is a short press

            // PB3 was released before 3 seconds while countdown was RUNNING
            if (held == PB3 && isShort && state == RUN) {
                countTimer_pause();     // PAUSE Timer 2/3 without resetting the count
                state = PAUSE;          // Enter PAUSE mode
                
                LATBbits.LATB9 = 0;     // Turn off LED1
                LATAbits.LATA6 = 0;     // Turn off LED2
                
            // PB3 was released before 3 seconds while countdown was PAUSED
            } else if (held == PB3 && isShort && state == PAUSE) {
                countTimer_resume();                        // RESUME Timer 2/3 without resetting the count
                pressTimer_start(0);                        // Restart 1-second ticks. Zero argument means its not tracking a long press
                state = RUN;                                // Return to RUN mode
                LATBbits.LATB9 = 1;                         // LED1 initially ON, resumes blinking
                LATAbits.LATA6 = 0;                         // KEEP alarm LED2 OFF
                
                // Read the remaining countdown time from Timer 2/3 and convert total seconds back to minutes and seconds for display
                uint16_t left = countTimer_remaining();     
                minutes = left / 60;
                seconds = left % 60;
                
                // Display the current time in RUN mode on the UART terminal
                Disp2String("\rCNT ");
                
                //Print minutes as two ASCII digits
                XmitUART2('0' + minutes / 10, 1);   // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
                XmitUART2('0' + minutes % 10, 1);   // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)
                
                Disp2String("m : ");
                
                //Print seconds as two ASCII digits
                XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
                XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
                
                Disp2String("s");
                
            // No pause/resume occurred
            } else if (state == RUN) {
                pressTimer_start(0);          // Restore Timer1's normal 1-second ticks
            }
            held = 0;                         // The button release has been processed, so clear the tracked button
        
        // If only PB3 is pressed and at least 1 button is pressed
        } else if (now == PB3 && added) {
            held = PB3;                       // Remember PB3 so its release can be handled later
            pressTimer_start(PB3);            // Start measuring PB3's press duration using Timer1, holding for 3 second triggers longPressButton
           
        // Ignore other button combinations during RUN/PAUSE
        } else {
            held = 0;         
        }
        return;
    }

    // All buttons released: process the previously tracked combination
    if (now == 0) {
        pressTimer_stop();      // Stop measuring the button press
        
        // Short PB1 + PB2 press starts the countdown
        // wasLong prevents a completed long press from triggering START
        if (held == START && !wasLong) {
            startCount();
            
        // All 3 buttons were released, restore the normal SET display after showing group info
        } else if (held == ALL) {
            // Display the current time in SET mode on the UART terminal
            Disp2String("\rSET ");              // Return to start of line and print SET
            
            XmitUART2('0' + minutes / 10, 1);   // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
            XmitUART2('0' + minutes % 10, 1);   // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)
            
            Disp2String("m : ");
            
            XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
            XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
            
            Disp2String("s               ");   // The spaces erase the leftover characters from longer messages
        }
        held = 0;
        return;
    }

    // The button combination changed but no new button was pressed
    // Means a partial release while another button remains down
    if (!added) {
        
        // Preserve START and ALL until every button is released
        // Other combinations are cancelled when partially released
        if (held != START && held != ALL) {
            pressTimer_stop();
            held = 0;
        }
        
        return;     // Wait for the remaining buttons to be released
    }

    // A new button press while FIN is active dismisses the alarm
    if (state == FIN) {
        state = SET;                        // Return state to SET mode
        pressTimer_stop();                  // Stop Timer 1's alarm blinking
        alarmTick = 0;                      // Clear any pending alarm event
        
        LATBbits.LATB9 = 0;                 // Turn LED1 OFF
        LATAbits.LATA6 = 0;                 // Turn LED2 OFF
        
        
        // Display the current time in SET mode on the UART terminal
        Disp2String("\rSET ");              // Return to start of line and print SET
            
        XmitUART2('0' + minutes / 10, 1);   // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
        XmitUART2('0' + minutes % 10, 1);   // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)
            
        Disp2String("m : ");
        
        XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
        XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
            
        Disp2String("s               ");   // The spaces erase the leftover characters from longer messages
    }
    
    // Begin tracking a new button press or combination
    held = now;
            
    fast = 0;                   // Reset fast-increment mode for the new press
    wasLong = 0;                // Reset the previous long-press indicator    
    pressTimer_start(now);      // Start Timer 1 to detect whether this combination is held for 3 seconds
    
    // All 3 buttons display the group information
    if (now == ALL) {
        Disp2String("\r2026 ENSF 460 L01 - Group 7");
    
    // Other combinations are passed to step() for time adjustment
    } else {
        step();                       // first step right away
    }
}

// Handle a button combination that has reached the 3-second hold threshold
void IOlong(void) {
    // If the countdown is running or paused, and PB3 has been held for 3 seconds
    if ((state == RUN || state == PAUSE) && held == PB3) {
        countTimer_clear();         // Stop and reset Timer 2/3 and countdown flags
        minutes = 0;                // Clear displayed minutes
        seconds = 0;                // Clear displayed seconds
        state = SET;                // Return to time-setting mode
        LATBbits.LATB9 = 0;         // LED1 OFF
        LATAbits.LATA6 = 0;         // LED2 OFF
        held = 0;                   // Clear the tracked press after handling the long press
        Disp2String("\rCLR 00m : 00s               ");      // Show cleared countdown
        return;
    }

    if (state != SET) return;       // Remaining long-press actions only apply in SET mode

    // If PB1 + PB2 is held then clear time
    if (held == START) {
        minutes = 0;                        // Set minutes to 0
        seconds = 0;                        // Set seconds to 0
        
         // Display the current time in SET mode on the UART terminal
        Disp2String("\rSET ");              // Return to start of line and print SET
            
        XmitUART2('0' + minutes / 10, 1);   // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
        XmitUART2('0' + minutes % 10, 1);   // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)
            
        Disp2String("m : ");
        
        XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
        XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
            
        Disp2String("s               ");   // The spaces erase the leftover characters from longer messages
        
        wasLong = 1;                        // Mark that START's long press action occurred
        held = 0;                           // Prevent release from triggering short START action
        return;
    }
    
    // After PB1 is held for 3 seconds, enable 5-second increments
    if (held == PB1) fast = 1;
    
    // Timer 1 stopped at the 3-second threshold
    // Restart it while a button is tracked so periodic ticks can continue
    if (held) pressTimer_start(held);
}

// Handle periodic Timer 1 events based on the current program state
void IOtick(void) {
    
    // SET mode: Repeat the selected button time adjustment on each Timer 1 tick while a valid button is held
    if (state == SET) {
        step();      
    
    } else if (state == RUN) {
        LATBbits.LATB9 ^= 1;                        // Toggle LED1 on each 1 second Timer 1 tick
        uint16_t left = countTimer_remaining();     // Read the actual remaining countdown time from Timer 2/3
        
        if (left == 0) return;                      // Do not refresh count at zero, countdown completion is handled by IOdone()
        
        // Convert total remaining seconds into minutes and seconds
        minutes = left / 60;                        
        seconds = left % 60;
        
        // Display the current time in RUN mode on the UART terminal
        Disp2String("\rCNT ");              // Return to start of line and print CNT
    
        //Print minutes as two ASCII digits
        XmitUART2('0' + minutes / 10, 1);    // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
        XmitUART2('0' + minutes % 10, 1);    // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)

        Disp2String("m : ");

        //Print seconds as two ASCII digits
        XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
        XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
    
        Disp2String("s");
        
    // FIN mode: Toggle LED2 every ~0.25 second for the alarm
    } else if(state == FIN){
        LATAbits.LATA6 ^= 1;
    }
}

void IOdone(void) {
    countDone = 0;                      // Clear the countdown completion flag after handling it
    if (state != RUN) return;           // Only a running countdown can transition into FIN mode

    pressTimer_stop();                  // Stop normal 1 second Timer 1 updates
    
    held = 0;                           // Clear any tracked button combination
    state = FIN;                        // Transition from RUN to FIN mode
    LATBbits.LATB9 = 1;                 // Keep LED1 ON after countdown completion
    LATAbits.LATA6 = 0;                 // Initialize LED2 OFF before alarm blinking
    alarmTimer_start();                 // Reconfigure Timer1 for ~0.25 second alarm ticks to blink LED2
    
    // Reset displayed countdown time to zero
    minutes = 0;
    seconds = 0;
    
    // Display the finished countdown and alarm message on UART
    Disp2String("\rFIN ");              // Return to start of line and print FIN
    
    //Print minutes as two ASCII digits
    XmitUART2('0' + minutes / 10, 1);    // 10's digit (minutes / 10 gives only the 10's digit of the minutes variable)
    XmitUART2('0' + minutes % 10, 1);    // 1's digit (minutes % 10 gives only the 1's digit of the minutes variable)

    Disp2String("m : ");

    //Print seconds as two ASCII digits
    XmitUART2('0' + seconds / 10, 1);   // 10's digit (seconds / 10 gives only the 10's digit of the seconds variable)
    XmitUART2('0' + seconds % 10, 1);   // 1's digit (seconds % 10 gives only the 1's digit of the seconds variable)
    
    Disp2String("s -  ALARM");
}

// Return 1 if the countdown is in FIN mode, otherwise return 0
uint8_t IOisFinished(void){
    return state == FIN;
}

// CN ISR: Executes when an enabled button input changes
void __attribute__((interrupt, no_auto_psv)) _CNInterrupt(void) {
    IFS1bits.CNIF = 0;          // Clear CN interrupt flag
    
    // In SET mode, wait ~100ms before processing button changes, to help recognize combinations
    if(state ==  SET){
        comboTimer_start();
        
    // Signal main to process button changes immediately
    } else {
        cnFlag = 1;
    }
    
}
