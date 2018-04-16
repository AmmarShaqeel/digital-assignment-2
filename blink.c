#include <msp430g2253.h>

int flag = 0;
int mode = 0;
int control = 0;
int low = 12;
int medium = 21;
int high = 25;
int *current;
int inc;
char numberStr[5];
char data;

void ConfigureTimer0A(void);
void ConfigureTimer1A(void);
void ConfigureUART(void);
void UARTSendArray(char *TxArray,  char ArrayLength);
char Int2DecStr(char *str, unsigned int value);


void main(void)
{
    WDTCTL = WDTPW + WDTHOLD;     // Stop Watchdog Timer
    BCSCTL1 = CALBC1_1MHZ; // Set DCO Clock to 1MHz
    DCOCTL = CALDCO_1MHZ;

    /*** GPIO Set-Up ***/
    P1OUT &= 0x00;               //Reset to 0
    P1DIR &= 0x00;               //Reset to 0
    P1SEL &= 0x00;

    P1DIR |= BIT0;                  //P1.0 output
    //P1DIR |= BIT6;                  //P1.6 output
    //P1SEL |= BIT6;                  // P1.6 TA1/2 options


    P1REN |= BIT3;                   // Enable internal pull-up/down resistors
    P1OUT |= BIT3;                   //Select pull-up mode for P1.3
    P1IE |= BIT3;                    // P1.3 interrupt enabled
    P1IES |= BIT3;                   // P1.3 Hi/lo edge
    P1IFG &= ~BIT3;                  // P1.3 IFG cleared

    ConfigureTimer0A();
    ConfigureTimer1A();
    ConfigureUART();

    _BIS_SR(CPUOFF + GIE);          // Enter LPM0 w/ interrupt

    UARTSendArray("Hello\n", 5);
}

// Timer A0 interrupt service routine
#pragma vector=TIMER0_A0_VECTOR
__interrupt void Timer_A (void)
{
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

#pragma vector=TIMER1_A0_VECTOR
__interrupt void Timer1_A (void)
{
   if(!flag){
       UARTSendArray("Standby\n", 9);
       P1OUT ^= BIT0; // Toggle P1.0
   }
   else {
       UARTSendArray("Active\n", 8);
       P1OUT &= ~BIT0;
   }
}

// Port 1 interrupt service routine
#pragma vector=PORT1_VECTOR
__interrupt void Port_1(void)
{
   P1OUT ^= BIT6;                      // Toggle P1.6
   P1IFG &= ~BIT3;                     // P1.3 IFG cleared

   if(!flag){
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
		   
       P1DIR |= BIT6;                  //P1.6 output
       P1SEL |= BIT6;
   }
   else {
       P1DIR &= ~BIT6;                  //P1.6 output
       P1SEL &= ~BIT6;
       TA0CCR1 &=0;                     //reset ta0ccr1
   }
   flag = ~flag;

 
}

void ConfigureTimer0A(void){
    /*** Timer0_A Set-Up ***/
    TA0CCR0 |= 1500-1;                  // Counter value
    TA0CTL |= TASSEL_2+ MC_1;       // ACLK, Up Mode (Counts to TA0CCR0)
    TA0CCTL1 |= OUTMOD_7;           // TA1CCR1 output mode = reset/set
}

void ConfigureTimer1A(void){
   /*** Timer1_A Set-Up ***/
    TA1CCR0 |= 3000 - 1;
    TA1CCTL0 |= CCIE;           // TA1CCR1 output mode = reset/set
    TA1CTL |= TASSEL_1 + MC_1;      // SMCLK, Up Mode (Counts to TA1CCR0)
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
