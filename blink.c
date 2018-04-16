#include <msp430g2253.h>
#define B1 0x0002
#define B2 0x0001


static int flag = 0;
static int mode = 0;
static int control = 0;
static int climate = 0;
static int low = 12;
static int medium = 21;
static int high = 25;
static int *current = &low;
static int newValue = 0;
static int i=0;



char numberStr[5];
char PressCountB1 = 0;
char PressCountB2 = 0;
char Pressed = 0;
char PressRelease = 0;
char data;

void UARTSendArray(char *TxArray,  char ArrayLength);
int atoi(const char *str);
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

// Timer A0 interrupt service routine (for blinking light)
#pragma vector=TIMER1_A0_VECTOR
__interrupt void Timer1_A (void)
{
    if(!flag){
        control++;
        if (control == 3){
            P1OUT ^= BIT0; // Toggle P1.0
            control = 0;
        }

    }
    else {
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

#pragma vector = NMI_VECTOR
__interrupt void nmi_isr(void)
{
    if (IFG1 & NMIIFG) // Check if NMI interrupt was caused by nRST/NMI pin
    {
        IFG1 &= ~NMIIFG; // clear NMI interrupt flag
        if (WDTCTL & WDTNMIES) // falling edge detected
        {
            Pressed |= B2; // Set Switch 2 Pressed flag
            PressCountB2 = 0; // Reset Switch 2 long press count
            WDTCTL = WDT_MDLY_32 | WDTNMI; // WDT 32ms delay + set RST/NMI pin to NMI
            // Note: WDT_MDLY_32 = WDTPW | WDTTMSEL | WDTCNTCL // WDT password + Interval mode + clear count
            // Note: this will also set the NMI interrupt to trigger on the rising edge

        }
        else // rising edge detected
        {
            Pressed &= ~B2; // Reset Switch 1 Pressed flag
            PressRelease |= B2; // Set Press and Released flag
            WDTCTL = WDT_MDLY_32 | WDTNMIES | WDTNMI; // WDT 32ms delay + falling edge + set RST/NMI pin to NMI
        }
    } // Note that NMIIE is now cleared; the wdt_isr will set NMIIE 32ms later
    else {/* add code here to handle other kinds of NMI, if any */}
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
                           UARTSendArray("Standby\n\r", 9);
            }

            else if (++PressCountB1 == 16 ) // Long press duration 32*32ms = 1s
            {
                TA0CCR1 &=0;                                             //reset ta0ccr1
                switch(mode){
                    case 0:
                        UARTSendArray("low\n\r", 5);
                        TA0CCR1 |= 1;
                        current = &low;
                        mode++;
                        break;
                    case 1:
                        UARTSendArray("medium\n\r", 8);
                        TA0CCR1 |= 500;
                        current = &medium;
                        mode++;
                        break;
                    case 2:
                        UARTSendArray("high\n\r", 6);
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
                    UARTSendArray("Active\n\r", 8);
           }
        }
   }

    if (Pressed & B2) // Check if switch 2 is pressed
      {
          if (++PressCountB2 == 32 ) // Long press duration 32ms*32 = 1s
          {
              climate = ~climate;
                      if(climate){
                          UARTSendArray("heat:", 4);
                          UARTSendArray("\n\r",2);
                      }
                      else{
                          UARTSendArray("cool:", 4);
                          UARTSendArray("\n\r",2);
                      }
          }
      }
    IFG1 &= ~NMIIFG; // Clear the NMI interrupt flag (in case it has been set by bouncing)
    P1IFG &= ~BIT3; // Clear the button interrupt flag (in case it has been set by bouncing)
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
    static char buffer[3];

    buffer[0] = buffer[1];
    buffer[1] = buffer[2];
    buffer[2] = UCA0RXBUF;
    data = UCA0RXBUF;

    UARTSendArray("Received command: ", 18);
    UARTSendArray(&data, 1);
    UARTSendArray("\n\r",2);

    if (buffer[2] == '\n' | buffer[2] == '\r' )
    {
        UARTSendArray(buffer, 3);
        UARTSendArray("\n\r",2);

        newValue = atoi(buffer);

        UARTSendArray(numberStr,Int2DecStr(numberStr, newValue));
        UARTSendArray("\n\r",2);

        if (newValue >= 5 && newValue <= 30)
        {
            *current = newValue;
        }
        else
        {
            UARTSendArray("Value out of range",18 );
            UARTSendArray("\n\r",2);
        }

        newValue = 0;
        UARTSendArray("L:", 2);
        UARTSendArray(numberStr,Int2DecStr(numberStr, low));
        UARTSendArray("\n\r",2);

        UARTSendArray("m:", 2);
        UARTSendArray(numberStr,Int2DecStr(numberStr, medium));
        UARTSendArray("\n\r",2);

        UARTSendArray("h:", 2);
        UARTSendArray(numberStr,Int2DecStr(numberStr, high));
        UARTSendArray("\n\r",2);

    }

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
    /* Configure hardware UART */
    P1SEL |= BIT1 + BIT2 ; // P1.1 = RXD, P1.2=TXD
    P1SEL2 |= BIT1 + BIT2 ; // P1.1 = RXD, P1.2=TXD
    UCA0CTL1 |= UCSSEL_2; // Use SMCLK
    UCA0BR0 = 0x68; // Set baud rate to 9600 with 1MHz clock (Data Sheet 15.3.13)
    UCA0BR1 = 0x00; // Set baud rate to 9600 with 1MHz clock
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
