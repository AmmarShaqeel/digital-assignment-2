#include <msp430g2553.h>

#define B1 0x0002
#define B2 0x0001


static int flag = 0;
static int mode = 0;
static int control = 0;
static int blinkLimit = 1;
static int override = 0;
static int blinkLimitOverride = 0;
static int low = 12;
static int medium = 21;
static int high = 25;
static int *current = &low;
static int newValue = 0;
int value = 0;
int reading = 21;



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
void ConfigureADC(void);


void main(void)
{   WDTCTL = WDTPW + WDTHOLD; //stop WDT
    BCSCTL1 = CALBC1_1MHZ;    //set DCO to 1MHz
    DCOCTL = CALDCO_1MHZ;     //set DCO to 1mhz

    P1DIR |= 0xFF;            //set all to outputs
    P2DIR |= 0xFF;            //set all to outputs
    P3DIR |= 0xFF;            //set all to outputs
    P1OUT &= 0x00;            //set all outputs to 0
    P2OUT &= 0x00;            //set all outputs to 0
    P3OUT &= 0x00;            //set all outputs to 0
    P1SEL &= 0x00;            //set p1sel to 0
    P1SEL |= BIT5;            //ADC input P1.5
    P1DIR |= BIT0;            //P1.0 output

    ConfigureTimer0A();
    ConfigureTimer1A();
    ConfigureUART();
    ConfigureSwitch();
    ConfigureADC();

	//setting up WDT to debounce button
    WDTCTL = WDTPW | WDTHOLD | WDTNMIES | WDTNMI; //WDT password + Stop WDT + detect RST button falling edge + set RST/NMI pin to NMI
    IFG1 &= ~(WDTIFG | NMIIFG);                   //reset WDT/NMI interrupt flags
    IE1 |= WDTIE | NMIIE;                         //Enable WDT/NMI interrupt
    _BIS_SR(CPUOFF + GIE);                        //Enter low power mode and enable interrupts
}

//Timer A0 interrupt service routine (for blinking light)
#pragma vector=TIMER1_A0_VECTOR
__interrupt void TIMER1A_ISR (void)
{
    if(!flag){
	//toggles p1 at 3 times duty cycle
        control++;
        if (control == 3){
            P1OUT ^= BIT0;  //Toggle P1.0
            control = 0;
        }

    }
    else {
	//if user is using RST button to override
        if(override){
            control++;
            if (control == blinkLimitOverride){
                P1OUT ^= BIT0;  //Toggle P1.0
                control = 0;
            }
        }
		//if user hasn't chosen mode then RED LED off
        else if (mode == 0)
        {
            P1OUT &= ~BIT0;
        }
        else{
            ADC10CTL0 |= ENC + ADC10SC;  //ADC Sampling and conversion start

            if(value > reading){
                blinkLimit = 6;
            }
            else if (value < reading){
                blinkLimit = 1;
            }
            else{
                blinkLimit = 0;
            }
            control++;
            if (control == blinkLimit){
                P1OUT ^= BIT0;  //Toggle P1.0
                control = 0;
            }
        }
    }
}

#pragma vector=PORT1_VECTOR
__interrupt void PORT1_ISR(void)
{
    if (P1IFG & BIT3){
        P1IE &= ~BIT3;                            //disable interrupts
        P1IFG &= ~BIT3;                           //clear interrupt flag
        if (P1IES & BIT3)
        {                                         //if falling edge detected
            Pressed |= B1;                        //set button1 flag
            PressCountB1 = 0;                     //Reset Switch 2 long press count
        }
        else
        {                                         //Rising edge detected
            Pressed &= ~B1;                       //Reset Switch 2 Pressed flag
            PressRelease |= B1;                   //Set Press and Released flag
        }
        P1IES ^= BIT3;                            //Toggle edge detect
        IFG1 &= ~WDTIFG;                          //Clear the interrupt flag for the WDT
        WDTCTL = WDT_MDLY_32 | (WDTCTL & 0x007F); //Restart the WDT with the same NMI status as set by the NMI interrupt
    }
}

#pragma vector = NMI_VECTOR
__interrupt void NMI_ISR(void)
{
    if (IFG1 & NMIIFG)  //Check if NMI interrupt was caused by nRST/NMI pin
    {
        IFG1 &= ~NMIIFG;       //clear NMI interrupt flag
        if (WDTCTL & WDTNMIES) //falling edge detected
        {
            Pressed |= B2;                 //Set Switch 2 Pressed flag
            PressCountB2 = 0;              //Reset Switch 2 long press count
            WDTCTL = WDT_MDLY_32 | WDTNMI; //WDT 32ms delay + set RST/NMI pin to NMI
			//Note: WDT_MDLY_32 = WDTPW | WDTTMSEL | WDTCNTCL
			//WDT password + Interval mode + clear count
			//Note: this will also set the NMI interrupt to trigger on the rising edge
        }
        else  //rising edge detected
        {
            Pressed &= ~B2;                           //Reset Switch 1 Pressed flag
            PressRelease |= B2;                       //Set Press and Released flag
            WDTCTL = WDT_MDLY_32 | WDTNMIES | WDTNMI; //WDT 32ms delay + falling edge + set RST/NMI pin to NMI
        }
    }  //Note that NMIIE is now cleared; the wdt_isr will set NMIIE 32ms later
}

#pragma vector = WDT_VECTOR
__interrupt void WDT_ISR(void)
{
    //Check if switch 2 is pressed
    if (Pressed & B1)
    {
        //if not in standby
        if(flag){  

            P1DIR |= BIT6;  //P1.6 output
            P1SEL |= BIT6;

            if (++PressCountB1 == 125){
                P1DIR &= ~BIT6;  //green LED (p1.6) off
                P1SEL &= ~BIT6;
                flag = ~flag;
                control = 0;
                UARTSendArray("Standby\n\r", 9);
            }

            else if (++PressCountB1 == 16)  //Long press duration 16*32ms = 0.5s
            {
                TA0CCR1 &=0;  //reset ta0ccr1
                switch(mode){
                    case 1:
                        UARTSendArray("low\n\r", 5);
                        TA0CCR1 |= 1;
                        current = &low;
                        mode++;
                        break;
                    case 2:
                        UARTSendArray("medium\n\r", 8);
                        TA0CCR1 |= 500;
                        current = &medium;
                        mode++;
                        break;
                    case 3:
                        UARTSendArray("high\n\r", 6);
                        TA0CCR1 |= 1499;
                        current = &high;
                        mode = 1;
                        break;
                    default:
                        mode = 1;

                }
            }
        }
        //if in standby
        else{
            if (++PressCountB1 == 16){
                flag = ~flag;
                P1OUT &= ~BIT0;
                control = 0;
                UARTSendArray("Active\n\r", 8);
            }
        }
    }
    
    //Check if switch 2 is pressed
    if (Pressed & B2)
    {
        if (++PressCountB2 == 32)  //Long press duration 32ms*32 = 1s
        {
			//if not in standby
            if(override){                                                   
                if (++PressCountB1 == 125){
                    override = 0;
                    control = 0;
                }

                else if (++PressCountB1 == 16 )  //Long press duration 32*32ms = 1s
                {
                    if (blinkLimitOverride == 1)
                    {blinkLimitOverride = 6;}
                    else
                    {blinkLimitOverride = 1;}

                }

            }
			//if in standby
            else{                                                           
                if (++PressCountB1 == 125){
                    override = 1;
                    control = 0;
                }
            }
        }
    }
    IFG1 &= ~NMIIFG; //Clear the NMI interrupt flag (in case it has been set by bouncing)
    P1IFG &= ~BIT3;  //Clear the button interrupt flag (in case it has been set by bouncing)
    IE1 |= NMIIE;    //Re-enable the NMI interrupt to detect the next edge
    P1IE |= BIT3;    //Re-enable interrupt for the button on P1.3
}



void UARTSendArray(char *TxArray, char ArrayLength){
//Send number of bytes Specified in ArrayLength in the array at using the hardware UART 0
//Example usage: UARTSendArray("Hello", 5);
//int data[2]={1023, 235};
//UARTSendArray(data, 4);                                                                        
//Note because the UART transmits bytes it is necessary to send two bytes for each integer hence the data length is twice the array length

    while(ArrayLength--){           //Loop until StringLength == 0 and post decrement
        while(!(IFG2 & UCA0TXIFG)); //Wait for TX buffer to be ready for new data
        UCA0TXBUF = *TxArray;       //Write the character at the location specified py the pointer
        TxArray++;                  //Increment the TxString pointer to point to the next character
    }
}

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

//ADC10 interrupt service routine
#pragma vector=ADC10_VECTOR
__interrupt void ADC10_ISR (void)
{
    value = ADC10MEM;  //read ADC value
    reading = 5 + (value-400)/600;
    UARTSendArray("V:", 2);
    UARTSendArray(numberStr,Int2DecStr(numberStr, value));
    UARTSendArray("\n\r",2);


}

static const unsigned int dec[] = {
    10000, //+5
    1000,  //+6
    100,   //+7
    10,    //+8
    1,     //+9
    0
};

char Int2DecStr(char *str, unsigned int value){
//Convert unsigned 16 bit binary integer to ascii character string
    char c;
    char n=0;
    unsigned int *dp = dec;

    while (value < *dp) dp++;  //Move to correct decade
    do {
        n++;
        c = 0;  //count binary
        while((value >= *dp) && (*dp!=0)) ++c, value -= *dp;
        *str++ = c+48;  //convert to ASCII
    }
    while(*dp++ >1);
    return n;
}

void ConfigureTimer0A(void){
    /* Timer0_A Set-Up */
    TA0CCR0 |= 1500-1;        //PWM period
    TA0CTL |= TASSEL_2+ MC_1; //ACLK, Up Mode (Counts to TA0CCR0)
    TA0CCTL1 |= OUTMOD_7;     //TA1CCR1 output mode = reset/set
}

void ConfigureTimer1A(void){
    /* Timer1_A Set-Up */
    TA1CCR0 |= 1200 - 1;
    TA1CCTL0 |= CCIE;          //TA1CCR1 output mode = reset/set
    TA1CTL |= TASSEL_1 + MC_1; //SMCLK, Up Mode (Counts to TA1CCR0)
}

void ConfigureUART(void){
    /* Configure hardware UART */
    P1SEL |= BIT1 + BIT2;  //P1.1 = RXD, P1.2=TXD
    P1SEL2 |= BIT1 + BIT2; //P1.1 = RXD, P1.2=TXD
    UCA0CTL1 |= UCSSEL_2;  //Use SMCLK
    UCA0BR0 = 0x68;        //Set baud rate to 9600 with 1MHz clock (Data Sheet 15.3.13)
    UCA0BR1 = 0x00;        //Set baud rate to 9600 with 1MHz clock
    UCA0MCTL = UCBRS0;     //Modulation UCBRSx = 1
    UCA0CTL1 &= ~UCSWRST;  //Initialize USCI state machine
    IE2 |= UCA0RXIE;       //Enable USCI_A0 RX interrupt
}

void ConfigureSwitch(void){
    /* Configure Switches */
    P1DIR &= ~BIT3; //Set button pin as an input pin
    P1OUT |= BIT3;  //Set pull up resistor on for button
    P1REN |= BIT3;  //Enable pull up resistor for button to keep pin high until pressed
    P1IES |= BIT3;  //Trigger on the falling edge
    P1IFG &= ~BIT3; //Clear the interrupt flag for the button
    P1IE |= BIT3;   //Enable interrupts on port 1 for the button
}

void ConfigureADC(void){
    /* Configure ADC  Channel */
    ADC10CTL1 = INCH_5;                                                     //Channel 5
    ADC10CTL0 = SREF_1 + ADC10SHT_2 + REFON + REF2_5V + ADC10ON + ADC10IE;  //Vref,adc clock 16 cycles, vref on, vref to 2.5V, ADC10 converter on, ADC interrupt enabled
    ADC10AE0 |= BIT5;                                                       //P1.5 ADC option
}
