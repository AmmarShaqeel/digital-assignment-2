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
int value = 500;



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
{
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
    IE1 |= WDTIE | NMIIE;                         //Enable interrupt
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
        else if (control >3){
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
            else if (control >blinkLimitOverride){
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

            if((*current)*34 > value){
                blinkLimit = 6;
            }
            else if ((*current)*34 < value){
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
            //avoid a situation where control increments forever
            else if (control >blinkLimit){
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
        {                                         //if falling edge
            Pressed |= B1;                        //set pressed	flag
            PressCountB1 = 0;                     //reset press count
        }
        else
        {                                         //Rising edge detected
            Pressed &= ~B1;                       //Reset pressed flag
            PressRelease |= B1;                   //Set pressRelease flag
        }
        P1IES ^= BIT3;                            //Toggle edge detect
        IFG1 &= ~WDTIFG;                          //Clear WDT interrupt flag 
        WDTCTL = WDT_MDLY_32 | (WDTCTL & 0x007F); //Restart the WDT with the same NMI status as set by the NMI interrupt
    }
}

#pragma vector = NMI_VECTOR
__interrupt void NMI_ISR(void)
{
    if (IFG1 & NMIIFG)  //Check if NMI interrupt was caused by nRST/NMI pin
    {
        IFG1 &= ~NMIIFG;       //clear interrupt flag
        if (WDTCTL & WDTNMIES) //if falling edge
        {
            Pressed |= B2;                 //set pressed flag
            PressCountB2 = 0;              //reset press count
            WDTCTL = WDT_MDLY_32 | WDTNMI; //WDT 32ms + set RST/NMI pin to NMI
        }
        else  //rising edge detected
        {
            Pressed &= ~B2;                           //Reset pressed flag
            PressRelease |= B2;                       //Set pressRelease flag
            WDTCTL = WDT_MDLY_32 | WDTNMIES | WDTNMI; //WDT 32ms delay + falling edge + set RST/NMI pin to NMI
        }
    } 
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
                UARTSendArray("Standby\n\r", 9);
            }

            else if (++PressCountB1 == 16)  //Long press duration 16*32ms = 0.5s
            {
                TA0CCR1 &=0;  //reset ta0ccr1
                switch(mode){
                    case 1:
                        UARTSendArray("low\n\r", 5);
                        TA0CCR1 |= 1;  //sets duty cycle low
                        current = &low;
                        mode++;
                        break;
                    case 2:
                        UARTSendArray("medium\n\r", 8);
                        TA0CCR1 |= 500;  //sets duty cycle to ~1/3 of period
                        current = &medium;
                        mode++;
                        break;
                    case 3:
                        UARTSendArray("high\n\r", 6);
                        TA0CCR1 |= 1499;  //duty cycle = period
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
                UARTSendArray("Active\n\r", 8);
            }
        }
    }
    
    //Check if switch 2 is pressed
    if (Pressed & B2)
    {
        if (++PressCountB2 == 16)  //press duration 32ms*32 = 1s
        {
			//toggles override
            if(override){                                                   
                if (++PressCountB1 == 125){
                    override = 0;
                }

                else if (++PressCountB1 == 16 )  //Long press duration 32*32ms = 1s
                {
                    if (blinkLimitOverride == 1)
                    {blinkLimitOverride = 6;}
                    else
                    {blinkLimitOverride = 1;}

                }

            }
			//toggles override
            else{                                                           
                if (++PressCountB1 == 125){
                    override = 1;
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
    while(ArrayLength--){           //Loop until StringLength == 0 and post decrement
        while(!(IFG2 & UCA0TXIFG)); //wait until txbuffer is ready
        UCA0TXBUF = *TxArray;       //write character to TxArray location
        TxArray++;                  //Increment to the next char
    }
}

#pragma vector=USCIAB0RX_VECTOR
__interrupt void USCI0RX_ISR(void)
{
    static char buffer[3];

	//stores last 3 entered values
    buffer[0] = buffer[1];
    buffer[1] = buffer[2];
    buffer[2] = UCA0RXBUF;
    data = UCA0RXBUF;

    UARTSendArray("Received command: ", 18);
    UARTSendArray(&data, 1);
    UARTSendArray("\n\r",2);

	//checks for newline or carriage return
    if (buffer[2] == '\n' | buffer[2] == '\r' )
    {
        UARTSendArray(buffer, 3);
        UARTSendArray("\n\r",2);

		//converts buffer to int
        newValue = atoi(buffer);

        UARTSendArray(numberStr,Int2DecStr(numberStr, newValue));
        UARTSendArray("\n\r",2);

		// limit for new value
        if (newValue >= 5 && newValue <= 30)
        {
            *current = newValue;
        }
        else
        {
            UARTSendArray("Value out of range",18 );
            UARTSendArray("\n\r",2);
        }

        newValue = 0;  //reset new val
		
		//print current thresholds
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
    TA0CTL |= TASSEL_2+ MC_1; //ACLK, Up Mode (Count to TA0CCR0)
    TA0CCTL1 |= OUTMOD_7;     //TA1CCR1 output mode = reset/set
}

void ConfigureTimer1A(void){
    /* Timer1_A Set-Up */
    TA1CCR0 |= 1200 - 1;	   //timer counts to this value
    TA1CCTL0 |= CCIE;          //enable interrupt
    TA1CTL |= TASSEL_1 + MC_1; //SMCLK, Up Mode (Count to TA1CCR0)
}

void ConfigureUART(void){
    /* Configure hardware UART */
    P1SEL |= BIT1 + BIT2;  //P1.1 RXD, P1.2 TXD
    P1SEL2 |= BIT1 + BIT2; //P1.1 RXD, P1.2 TXD
    UCA0CTL1 |= UCSSEL_2;  //Use SMCLK
    UCA0BR0 = 0x68;        //baud rate to 9600 (1MHz clk)
    UCA0BR1 = 0x00;        //baud rate to 9600 (1MHz clk)
    UCA0MCTL = UCBRS0;     //Modulation UCBRSx = 1
    UCA0CTL1 &= ~UCSWRST;  //Initialize USCI state machine
    IE2 |= UCA0RXIE;       //Enable interrupt
}

void ConfigureSwitch(void){
    /* Configure Switches */
    P1DIR &= ~BIT3; //Set button as input 
    P1OUT |= BIT3;  //Pull up resistor on 
    P1REN |= BIT3;  //Enable pull up resistor
    P1IES |= BIT3;  //Trigger on falling edge
    P1IFG &= ~BIT3; //Clear interrupt flag 
    P1IE |= BIT3;   //Enable interrupt
}

void ConfigureADC(void){
    /* Configure ADC  Channel */
    ADC10CTL1 = INCH_5;                                                     //Channel 5
    ADC10CTL0 = SREF_1 + ADC10SHT_2 + REFON + REF2_5V + ADC10ON + ADC10IE;  //Vref,adc clock 16 cycles, vref on, vref to 2.5V, ADC10 converter on, ADC interrupt enabled
    ADC10AE0 |= BIT5;                                                       //P1.5 ADC option
}
