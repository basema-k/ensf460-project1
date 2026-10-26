/*
 * File:   main.c
 * Author: Basema Khan, Michelle Yoon, Vincent Fong
 *
 *
 */
// FBS
#pragma config BWRP = OFF               // Table Write Protect Boot (Boot segment may be written)
#pragma config BSS = OFF                // Boot segment Protect (No boot program Flash segment)
 
// FGS
#pragma config GWRP = OFF               // General Segment Code Flash Write Protection bit (General segment may be written)
#pragma config GCP = OFF                // General Segment Code Flash Code Protection bit (No protection)
 
// FOSCSEL
#pragma config FNOSC = FRC              // Oscillator Select (Fast RC oscillator (FRC))
#pragma config IESO = OFF               // Internal External Switch Over bit (Internal External Switchover mode disabled (Two-Speed Start-up disabled))
 
// FOSC
#pragma config POSCMOD = NONE           // Primary Oscillator Configuration bits (Primary oscillator disabled)
#pragma config OSCIOFNC = ON            // CLKO Enable Configuration bit (CLKO output disabled; pin functions as port I/O)
#pragma config POSCFREQ = HS            // Primary Oscillator Frequency Range Configuration bits (Primary oscillator/external clock input frequency greater than 8 MHz)
#pragma config SOSCSEL = SOSCHP         // SOSC Power Selection Configuration bits (Secondary oscillator configured for high-power operation)
#pragma config FCKSM = CSECMD           // Clock Switching and Monitor Selection (Clock switching is enabled, Fail-Safe Clock Monitor is disabled)
 
// FWDT
#pragma config WDTPS = PS32768          // Watchdog Timer Postscale Select bits (1:32,768)
#pragma config FWPSA = PR128            // WDT Prescaler (WDT prescaler ratio of 1:128)
#pragma config WINDIS = OFF             // Windowed Watchdog Timer Disable bit (Standard WDT selected; windowed WDT disabled)
#pragma config FWDTEN = OFF             // Watchdog Timer Enable bit (WDT disabled (control is placed on the SWDTEN bit))
 
// FPOR
#pragma config BOREN = BOR3             // Brown-out Reset Enable bits (Brown-out Reset enabled in hardware; SBOREN bit disabled)
#pragma config PWRTEN = ON              // Power-up Timer Enable bit (PWRT enabled)
#pragma config I2C1SEL = PRI            // Alternate I2C1 Pin Mapping bit (Default location for SCL1/SDA1 pins)
#pragma config BORV = V18               // Brown-out Reset Voltage bits (Brown-out Reset set to lowest voltage (1.8V))
#pragma config MCLRE = ON               // MCLR Pin Enable bit (MCLR pin enabled; RA5 input pin disabled)
 
// FICD
#pragma config ICS = PGx2               // ICD Pin Placement Select bits (PGC2/PGD2 are used for programming and debugging the device)
 
// FDS
#pragma config DSWDTPS = DSWDTPSF       // Deep Sleep Watchdog Timer Postscale Select bits (1:2,147,483,648 (25.7 Days))
#pragma config DSWDTOSC = LPRC          // DSWDT Reference Clock Select bit (DSWDT uses LPRC as reference clock)
#pragma config RTCOSC = SOSC            // RTCC Reference Clock Select bit (RTCC uses SOSC as reference clock)
#pragma config DSBOREN = ON             // Deep Sleep Zero-Power BOR Enable bit (Deep Sleep BOR enabled in Deep Sleep)
#pragma config DSWDTEN = ON             // Deep Sleep Watchdog Timer Enable bit (DSWDT enabled)
 




#include <xc.h>
#include <p24F16KA101.h>
#include "clkChange.h"
#include "UART2.h"
#include "IOs.h"
#include "TimeDelay.h"

int main(void) {
    AD1PCFG = 0xFFFF;           // All pins digital
    newClk(500);                // Set oscillator to ~500 kHz -> UART at 4800 baud
    InitUART2();                // Initialize UART2 for terminal output
    IOinit();                   // Configure buttons, LEDs, pull-ups, and CN interrupts
    timer1_init();              // Configure Timer 1 and its interrupt 

    IOstart();                  // Display the initial SET 00m : 00s message

    while (1) {

        // UART2.c turns on its TX interrupt each time it sends. Turn it off
        // so it can't wake the CPU and be mistaken for a Timer1 tick.
        IEC1bits.U2TXIE = 0;
        IFS1bits.U2TXIF = 0;

        // Enter low-power Idle mode only when no interrupt-generated events are waiting to be processed
        if (!cnFlag && !longPressButton && !countDone && !alarmTick && !comboDone && !normalTick) {
            Idle();       // Sleep until an interrupt occurs
        }

        // Handle countdown completion signaled by the Timer 3 ISR
        if (countDone) {
            IOdone();
        }

        // Process button changes signaled by the CN interrupt
       if (cnFlag) {
           cnFlag = 0;                 // Clear the pending button change flag

           if (IOisSetting()) {
               comboTimer_start();     // SET mode: wait ~100 ms so button combinations can settle
           } else {
             IOcheck();              // Other modes: handle the change right away
           }
        }
        
        // Process SET mode button combinations after the ~100ms detection window
        if(comboDone){
            comboDone = 0;      // Clear the completed combination event
            IOcheck();          // Read the final button combination
        }

        // Handle a 3-second button hold detected by Timer 1
        if (longPressButton) { 
            longPressButton = 0;        // Clear the long-press event flag
            IOlong();                   // Execute the state-dependent long-press action
        }
        
        // Handle ~0.25 second alarm ticks by updating the FIN state LED
        if(alarmTick){              
            alarmTick = 0;          // Clear the pending alarm tick flag
            IOtick();               // Toggle LED2 while in FIN mode
        }

        // Handle normal 1 second Timer 1 events
        // Updates held-button adjustments or the running countdown display
        if (normalTick) {
            normalTick = 0;     // Clear the pending one second tick
            IOtick();           // Perform the state-dependent periodic update
        }
    }
    return 0;
}
