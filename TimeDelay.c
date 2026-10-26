/*
 * File:   TimeDelay.c
 * Author: Basema Khan, Michelle Yoon, Vincent Fong
 */

#include "xc.h"
#include "TimeDelay.h"

// Fcy = 250 kHz (newClk(500)), 1:8 prescaler on all timers
// 31250 ticks per second
#define TICKS_PER_SEC 31250

static volatile uint8_t seconds     = 0;        // Counts Timer 1's one second intervals for button timing
static volatile uint8_t timedButton = 0;        // Stores the button combination being timed
static uint32_t countPeriod = 0;                // Stores the total Timer 2/3 countdown duration

volatile uint8_t longPressButton = 0;           // Signals that a three second hold was detected
volatile uint8_t countDone = 0;                 // Signals that Timer 2/3 finished the countdown
static volatile uint8_t alarmMode = 0;          // Indicates Timer 1 is generating alarm ticks
volatile uint8_t alarmTick = 0;                 // Signals that an alarm tick occurred
static volatile uint8_t comboMode = 0;          // Indicates Timer 1 is measuring the 100ms combination window
volatile uint8_t comboDone = 0;                 // Signals that the combination-detection window ended
volatile uint8_t normalTick = 0;                // Signals that a normal one second Timer1 tick occurred

// Configure Timer 1 with 1:8 pre-scaler
void timer1_init(void) {
    T1CONbits.TON   = 0;        // Stop Timer 1 before configuring it
    T1CONbits.TCS   = 0;        // Use the internal instruction clock (Fcy)
    T1CONbits.TCKPS = 1;        // 1:8 pre-scaler
    TMR1 = 0;                   // Reset Timer 1's counter
    PR1  = TICKS_PER_SEC;       // Configure 1 second Timer 1 period

    IPC0bits.T1IP = 4;          // Set Timer1 priority to level 4
    IFS0bits.T1IF = 0;          // Clear Timer 1 interrupt flag
    IEC0bits.T1IE = 1;          // Enable Timer 1 interrupts 
}

// Start a ~100ms window to recognize closely spaced button presses
void comboTimer_start(void){
    T1CONbits.TON = 0;          // Stop Timer 1 before reconfiguring it
        
    comboMode = 1;              // Tell Timer 1 ISR that combination detection is active
    comboDone = 0;              // Clear previous combination complete flag
    
    TMR1 = 0;                   // Reset Timer 1
    PR1 = 3125;                 // Set ~100ms period
    IFS0bits.T1IF = 0;          // Clear any pending Timer 1 interrupt
    T1CONbits.TON = 1;          // Start the ~100ms combination timer
}

// Start Timer 1 with 1 seconds ticks
// A nonzero button argument enables 3-second long press detection
// PAssing 0 produces normal 1 second ticks without tracking button press duration
void pressTimer_start(uint8_t button) {
    T1CONbits.TON = 0;      // Turn off Timer 1

    // Disable Timer 1's previous alarm and combination detection modes
    alarmMode = 0;          
    alarmTick = 0;
    comboMode = 0;
    comboDone = 0;

    PR1 = 31250;            // Restore the 1 second Timer 1 period
    timedButton = button;   // Track the button, 0 disables long-press detection
    seconds = 0;            // Reset the number of elapsed 1 second ticks
    TMR1 = 0;               // Reset Timer 1
    IFS0bits.T1IF = 0;      // Clear the Timer 1 interrupt flag
    T1CONbits.TON = 1;      // Start Timer 1 with new settings
}

// Stop Timer 1 and return whether it was running before stopping, it is used to distinguish between short and long presses
uint8_t pressTimer_stop(void) {
    uint8_t wasRunning = T1CONbits.TON;         // Save Timer 1's current running state
    T1CONbits.TON = 0;                          // Stop Timer 1
    return wasRunning;                          // Return 1 if Timer 1 was running, otherwise 0
}

void __attribute__((interrupt, no_auto_psv)) _T1Interrupt(void) {
    IFS0bits.T1IF = 0;              // Clear Timer 1 interrupt flag
    
    //Combination mode: The ~100ms button detection window has ended
    if(comboMode){
        T1CONbits.TON = 0;          // Stop the single run combination Timer
        comboMode = 0;              // Exit combination mode
        comboDone = 1;              // Signal to main to process the button combination
        return;                     // Return to not process it like a normal Timer 1 one second tick
    }
    
    // Alarm mode: Generate a repeated ~0.25 second event for LED2 blinking
    if(alarmMode){
        alarmTick = 1;              // Signal to main to toggle LED2 through IOtick()
        return;                     // Return to not process it like a normal Timer 1 one second tick
    }
    
    seconds++;                      // Count how many one second Timer 1 interrupts occur
    normalTick = 1;                 // Signal to main to perform a a normal periodic update

    // Detect a 3-second hold only when a button is being tracked
    if (seconds >= 3 && timedButton != 0) {
        T1CONbits.TON = 0;                  // Stop Timer 1 after detecting a 3 second hold
        longPressButton = timedButton;      // Signal to main that a long press occurred
    }
}

// Start the countdown using Timer 2 and Timer 3 in 32-bit mode
// Converts the inputted duration into ticks and configures Timer 3 to interrupt when the countdown finishes
void countTimer_start(uint16_t totalSeconds) {
    // Ignore a zero second countdown
    if (totalSeconds == 0) {
        return;                    // nothing to count
    }

    // Convert seconds to ticks/second using 32-bit arithmetic to prevent overflow for longer countdown durations
    countPeriod = (uint32_t)totalSeconds * TICKS_PER_SEC;

    T2CONbits.TON = 0;          // Stop Timer 2/3 before configuration
    T2CONbits.T32 = 1;          // Combine Timer 2 + Timer 3
    T2CONbits.TCS = 0;          // Use internal instruction clock
    T2CONbits.TCKPS = 1;        // Select 1:8 pre-scaler

    // Reset both halves of the 32-bit Timer 2/3 counter
    TMR3HLD = 0;                
    TMR2    = 0;
    
    // Split the 32-bit countdown period across two 16-bit registers
    PR3 = (uint16_t)(countPeriod >> 16);        // Upper 16 bits
    PR2 = (uint16_t)countPeriod;                // Lower 16 bits

    IPC2bits.T3IP = 3;          // Set Timer 3 interrupt priority to 3
    IFS0bits.T3IF = 0;          // Clear pending Timer 3 interrupt flag
    IEC0bits.T3IE = 1;          // Enable Timer 3 interrupts for countdown completion

    countDone = 0;              // Reset the countdown completion flag
    T2CONbits.TON = 1;          // Start the 32-bit countdown timer
}

// Pause the countdown without resetting the current Timer 2/3 count
void countTimer_pause(void) {
    T2CONbits.TON = 0;
}

// Resume a paused countdown without resetting elapsed time
void countTimer_resume(void) {
    
    // Only restart the countdown if it is not finished
    if (!countDone && countPeriod != 0) {
        T2CONbits.TON = 1;
    }
}

// Completely stop and reset the 32-bit countdown timer
void countTimer_clear(void){
    T2CONbits.TON = 0;          // Stop Timer 2/3
    
    // Reset both halves of the 32-bit Timer 2/3 counter
    TMR2 = 0;                
    TMR3HLD = 0;
    
    countPeriod = 0;            // Remove the configured countdown duration
    countDone = 0;              // Clear countdown completion flag
    IFS0bits.T3IF = 0;          // Clear Timer 3 interrupt flag
}

// Seconds left, rounded up so it shows 1 until the time actually ends
uint16_t countTimer_remaining(void) {
    
    // Returns zero if the countdown is complete or no period is configured
    if (countDone || countPeriod == 0) {
        return 0;
    }

    uint16_t lo = TMR2;             // Read the lower 16-bits of Timer 2/3 current count
    uint16_t hi = TMR3HLD;          // Read the upper 16-bits of Timer 2/3 current count
    uint32_t elapsed = ((uint32_t)hi << 16) | lo;       // Reconstruct the 32-bit elapsed tick count from both Timer registers

    // Return zero if the elapsed count has reached the countdown period
    if (elapsed >= countPeriod) {
        return 0;
    }

    uint32_t left = countPeriod - elapsed;      // Subtract elapsed ticks from the total ticks to get remaining ticks
    return (uint16_t)((left + TICKS_PER_SEC - 1) / TICKS_PER_SEC);      // Convert remaining ticks to seconds
}

// Reconfigure Timer 1 for ~0.25 second alarm ticks
// Signal to main to toggle LED2 while in FIN mode
void alarmTimer_start(void){
    T1CONbits.TON = 0;          // Stop Timer 1 before reconfiguration
    
    alarmMode = 1;              // Enable alarm mode in the Timer 1 ISR
    alarmTick = 0;              // Clear pending alarm events
    
    seconds = 0;                // Reset Timer 1's elapsed seconds counter
    timedButton = 0;            // Disable long-press tracking
    
    TMR1 = 0;                   // Reset Timer 1 counter
    PR1 = 7812;                 // Set ~0.25 second timer period
    
    IFS0bits.T1IF = 0;          // Clear pending Timer 1 interrupt flag
    T1CONbits.TON = 1;          // Start Timer 1 in alarm mode
}

// Timer 3 ISR: Executes when the 32-bit Timer 2/3 countdown reaches its period
void __attribute__((interrupt, no_auto_psv)) _T3Interrupt(void) {
    IFS0bits.T3IF = 0;              // Clear the Timer 3 interrupt flag
    T2CONbits.TON = 0;              // Stop the completed countdown timer
    countDone = 1;                  // Signal to main to handle the completed countdown
}
