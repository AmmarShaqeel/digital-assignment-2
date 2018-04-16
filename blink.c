#include <msp430g2253.h>
#define B1 0x0002

static int flag = 0;
static int mode = 0;
static int control = 0;
static int low = 12;
static int medium = 21;
static int high = 25;
static int *current;




char numberStr[5];

char PressCountB1 = 0;
char Pressed = 0;
char PressRelease = 0;
char data;

void UARTSendArray(char *TxArray,  char ArrayLength);
char Int2DecStr(char *str, unsigned int value);
void ConfigureTimer0A(void);
void ConfigureTimer1A(void);
void ConfigureUART(void);
void ConfigureSwitch(void);


void main(void)
{
    WDTCTL = WDTPW + WDTHOLD; // Stop WDT
    BCSCTL1 = CALBC1_1MHZ;        // Set DCO Clock to 1MHz
    DCOCTL = CALDCO_1MHZ;

    /*** GPIO Set-Up ***/
    P1DIR |= 0xFF;               //set all to outputs
    P2DIR |= 0xFF;               //set all to outputs
    P3DIR |= 0xFF;               //set all to outputs
    P1OUT &= 0x00;               //Reset to 0
    P2OUT &= 0x00;
    P3OUT &= 0x00;

    P1SEL &= 0x00;
    P1DIR |= BIT0;                  //P1.0 output

    ConfigureTimer0A();
    ConfigureTimer1A();
    ConfigureUART();
    ConfigureSwitch();

    // The Watchdog Timer (WDT) will be used to debounce s1 and B1
    WDTCTL = WDTPW | WDTHOLD | WDTNMIES | WDTNMI; //WDT password + Stop WDT + detect RST button falling edge + set RST/NMI pin to NMI
    IFG1 &= ~(WDTIFG | NMIIFG); // Clear the WDT and NMI interrupt flags
    IE1 |= WDTIE | NMIIE; // Enable the WDT and NMI interrupts

    _BIS_SR(CPUOFF + GIE);          // Enter LPM0 w/ interrupt
}

// Timer A0 interrupt service routine (for green LED pwm)
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A (void)
{
    static int inc;
    if(!flag){
    }
    else {
        //if CCR1 is at 1 or at CCR0 minus 1, toggle the value of inc
        if ((TA0CCR1 == (TA1CCR0 - 1)) || (TA0CCR1 == 1)) {
            inc = !inc;
        }
        // if inc is 1 increment CCR1, else decrement CCR1
        if (inc) {
            TA0CCR1++;
        }
        else {
            TA0CCR1--;
        }
        TA0CTL &= ~TAIFG;
    }
}

// Timer A0 interrupt service routine (for blinking light)
#pragma vector=TIMER1_A0_VECTOR
__interrupt void Timer1_A (void)
{
    if(!flag){
        control++;
        if (control == 3){
            UARTSendArray("Standby\n", 9);
            P1OUT ^= BIT0; // Toggle P1.0
            control = 0;
        }

    }
    else {
        UARTSendArray("Active\n", 8);
        P1OUT &= ~BIT0;
    }
}

#pragma vector=PORT1_VECTOR
__interrupt void PORT1_ISR(void)
{
    if (P1IFG & BIT3)
    {
        P1IE &= ~BIT3; // Disable Button interrupt to avoid bounces
        P1IFG &= ~BIT3; // Clear the interrupt flag for the button
        if (P1IES & BIT3)
        { // Falling edge detected
            Pressed |= B1; // Set Switch 2 Pressed flag
            PressCountB1 = 0; // Reset Switch 2 long press count
        }
        else
        { // Rising edge detected
            Pressed &= ~B1; // Reset Switch 2 Pressed flag
            PressRelease |= B1; // Set Press and Released flag
        }
        P1IES ^= BIT3; // Toggle edge detect
        IFG1 &= ~WDTIFG; // Clear the interrupt flag for the WDT
        WDTCTL = WDT_MDLY_32 | (WDTCTL & 0x007F); // Restart the WDT with the same NMI status as set by the NMI interrupt
    }
    else {/* add code here to handle other PORT1 interrupts, if any */}
}

#pragma vector = WDT_VECTOR
__interrupt void wdt_isr(void)
{
    if (Pressed & B1) // Check if switch 2 is pressed
    {
        if(flag){ //if not in standby

            P1DIR |= BIT6;                  //P1.6 output
            P1SEL |= BIT6;

            if (++PressCountB1 == 125){
                           P1DIR &= ~BIT6;                  //green LED (p1.6) off
                           P1SEL &= ~BIT6;
                           flag = ~flag;
            }

            else if (++PressCountB1 == 16 ) // Long press duration 32*32ms = 1s
            {
                TA0CCR1 &=0;                                             //reset ta0ccr1
                switch(mode){
                    case 0:
                        UARTSendArray("low\n", 5);
                        TA0CCR1 |= 1;
                        current = &low;
                        mode++;
                        break;
                    case 1:
                        UARTSendArray("medium\n", 8);
                        TA0CCR1 |= 500;
                        current = &medium;
                        mode++;
                        break;
                    case 2:
                        UARTSendArray("high\n", 6);
                        TA0CCR1 |= 1499;
                        current = &high;
                        mode = 0;
                        break;
                }
            }
        }
        else{ //if in standby
            if (++PressCountB1 == 16){
                    flag = ~flag;
                    P1OUT &= ~BIT0;
           }
        }
   }
    IE1 |= NMIIE; // Re-enable the NMI interrupt to detect the next edge
    P1IE |= BIT3; // Re-enable interrupt for the button on P1.3
}



void UARTSendArray(char *TxArray, char ArrayLength){
    // Send number of bytes Specified in ArrayLength in the array at using the hardware UART 0
    // Example usage: UARTSendArray("Hello", 5);
    // int data[2]={1023, 235};
    // UARTSendArray(data, 4); // Note because the UART transmits bytes it is necessary to send two bytes for each integer hence the data length is twice the array length

    while(ArrayLength--){ // Loop until StringLength == 0 and post decrement
        while(!(IFG2 & UCA0TXIFG)); // Wait for TX buffer to be ready for new data
        UCA0TXBUF = *TxArray; //Write the character at the location specified py the pointer
        TxArray++; //Increment the TxString pointer to point to the next character
    }
}

// Echo back RXed character, confirm TX buffer is ready first
#pragma vector=USCIAB0RX_VECTOR
__interrupt void USCI0RX_ISR(void)
{
    data = UCA0RXBUF;
    UARTSendArray("Received command: ", 18);
    UARTSendArray(&data, 1);
    UARTSendArray("\n", 2);

    *current = atoi(&data);

    UARTSendArray("L:", 2);
    UARTSendArray(numberStr,Int2DecStr(numberStr, low));
    UARTSendArray("\n", 2);

    UARTSendArray("m:", 2);
    UARTSendArray(numberStr,Int2DecStr(numberStr, medium));
    UARTSendArray("\n", 2);

    UARTSendArray("h:", 2);
    UARTSendArray(numberStr,Int2DecStr(numberStr, high));
    UARTSendArray("\n", 2);
}

static const unsigned int dec[] = {
    10000, // +5
    1000, // +6
    100, // +7
    10, // +8
    1, // +9
    0
};

char Int2DecStr(char *str, unsigned int value){
    // Convert unsigned 16 bit binary integer to ascii character string

    char c;
    char n=0;
    unsigned int *dp = dec;

    while (value < *dp) dp++; // Move to correct decade
    do {
        n++;
        c = 0; // count binary
        while((value >= *dp) && (*dp!=0)) ++c, value -= *dp;
        *str++ = c+48; //convert to ASCII
    }
    while(*dp++ >1);
    return n;
}

void ConfigureTimer0A(void){
    /*** Timer0_A Set-Up ***/
    TA0CCR0 |= 1500-1;              // PWM period
    TA0CTL |= TASSEL_2+ MC_1;       // ACLK, Up Mode (Counts to TA0CCR0)
    TA0CCTL1 |= OUTMOD_7;           // TA1CCR1 output mode = reset/set
}

void ConfigureTimer1A(void){
    /*** Timer1_A Set-Up ***/
    TA1CCR0 |= 1200 - 1;
    TA1CCTL0 |= CCIE;           // TA1CCR1 output mode = reset/set
    TA1CTL |= TASSEL_1 + MC_1;      // SMCLK, Up Mode (Counts to TA1CCR0)
}

void ConfigureUART(void){
#define RXD BIT1
#define TXD BIT2
    /* Configure hardware UART */
    P1SEL |= RXD + TXD ; // P1.1 = RXD, P1.2=TXD
    P1SEL2 |= RXD + TXD ; // P1.1 = RXD, P1.2=TXD
    UCA0CTL1 |= UCSSEL_2; // Use SMCLK
    UCA0BR0 = 104; // Set baud rate to 9600 with 1MHz clock (Data Sheet 15.3.13)
    UCA0BR1 = 0; // Set baud rate to 9600 with 1MHz clock
    UCA0MCTL = UCBRS0; // Modulation UCBRSx = 1
    UCA0CTL1 &= ~UCSWRST; // Initialize USCI state machine
    IE2 |= UCA0RXIE; // Enable USCI_A0 RX interrupt
}

void ConfigureSwitch(void){
    P1DIR &= ~BIT3; // Set button pin as an input pin
    P1OUT |= BIT3; // Set pull up resistor on for button
    P1REN |= BIT3; // Enable pull up resistor for button to keep pin high until pressed
    P1IES |= BIT3; // Enable Interrupt to trigger on the falling edge (high (unpressed) to low (pressed) transition)
    P1IFG &= ~BIT3; // Clear the interrupt flag for the button
    P1IE |= BIT3; // Enable interrupts on port 1 for the button
}
