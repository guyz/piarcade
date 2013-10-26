#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>

// MCP23S17 Registers

#define IOCON           0x0A

#define IODIRA          0x00
#define IPOLA           0x02
#define GPINTENA        0x04
#define DEFVALA         0x06
#define INTCONA         0x08
#define GPPUA           0x0C
#define INTFA           0x0E
#define INTCAPA         0x10
#define GPIOA           0x12
#define OLATA           0x14

#define IODIRB          0x01
#define IPOLB           0x03
#define GPINTENB        0x05
#define DEFVALB         0x07
#define INTCONB         0x09
#define GPPUB           0x0D
#define INTFB           0x0F
#define INTCAPB         0x11
#define GPIOB           0x13
#define OLATB           0x15

// Bits in the IOCON register

#define IOCON_BANK_MODE 0x80
#define IOCON_MIRROR    0x40
#define IOCON_SEQOP     0x20
#define IOCON_DISSLW    0x10
#define IOCON_HAEN      0x08
#define IOCON_ODR       0x04
#define IOCON_INTPOL    0x02
#define IOCON_UNUSED    0x01

// Default initialisation mode

#define IOCON_INIT      (IOCON_SEQOP)

#define BUTTON_PIN 0
#define N_MCP_ROWS 2
#define N_ROW_PINS 8

struct {
  int pin; // 0-15, 0-7 is GPIOA, 8-15 is GPIOB
  int key;
} io[] = {
//    Input    Output (from /usr/include/linux/input.h)
  { 0,      1     },
  {  1,      1    },
  {  5,      1    }
};
#define IOLEN (sizeof(io) / sizeof(io[0])) // io[] table size

// MCP23017 GPIO 8-bit row context
typedef struct{
  int idx; // row idx, A=0, B=1
  int inmask;
  int lastvalue;
  int key_value[N_ROW_PINS]; // -1=unused, 0=low, 1=high
} mcp_row;

mcp_row mcp[N_MCP_ROWS];

int q2w;

void register_mcp_keys() {  
  int i;
  for (i=0; i<N_MCP_ROWS; i++) {
    mcp[i].inmask = 0;
    memset(mcp[i].key_value, -1, sizeof(mcp[i].key_value));
  }

  for(i=0; i<IOLEN; i++) {
    printf("Configuring pin %d\n", io[i].pin);
    int idx = (floor(io[i].pin/N_ROW_PINS));
    int pin = io[i].pin % N_ROW_PINS;
    mcp[idx].inmask |= 1<<pin;
    mcp[idx].key_value[pin] = 1; // default is high
  }

  for (i=0; i<N_MCP_ROWS; i++) {
    mcp[i].lastvalue = mcp[i].inmask;
  }

  printf("Row A=%d, Row B=%d\n", mcp[0].inmask, mcp[1].inmask);

  // int j;
  // for (i=0; i<N_MCP_ROWS; i++) {
  //   for (j=0; j<N_ROW_PINS; j++) {
  //     printf("Row=%d, pin=%d, value=%d", i, j, mcp[i].key_value[j]);
  //   }
  // }
}

void mcp_interrupt_handler (void) { 
struct wiringPiNodeStruct *myNode ;
int val[N_MCP_ROWS], ival[N_MCP_ROWS], xval[N_MCP_ROWS], i, j, x, f;
//if((myNode = wiringPiFindNode (BUTTON_PIN)) == 0) {
//printf("wiringPiFindNode failed\n\r");
//exit(0);
//}
// printf("I've been hit!!\n\r");

// read values
val[0] = wiringPiI2CReadReg8 (q2w, GPIOA);
val[1] = wiringPiI2CReadReg8 (q2w, GPIOB);

// collect events
while ( (val[0] != mcp[0].inmask) && (val[1] != mcp[1].inmask) ) {
for (i=0; i<N_MCP_ROWS; i++) {
  ival[i] = val[i] & mcp[i].inmask; // current value
  xval[i] = ival[i] ^ mcp[i].lastvalue; // changes; a bit=1 if value changed
  mcp[i].lastvalue = ival[i];
  // printf("i=%d, val=%d, ival=%d, xval=%d\n", i, val[i], ival[i], xval[i]);
  for (j=0; j<N_ROW_PINS; j++) {
    if (mcp[i].key_value[j] > -1) {
      // pin is enabled
      x = !(ival[i] & (1 << j)); /* is the pin high or low? */
      f = xval[i] & (1 << j); /* has the pin changed? */
      if (f) {
        printf("Pin %d changed - %d!\n", j, x);
      }
    }
  }
}

val[0] = wiringPiI2CReadReg8 (q2w, GPIOA);
val[1] = wiringPiI2CReadReg8 (q2w, GPIOB);

}

printf("Done");

}

int main (int argc, char *argv [])
{
  int gpiofd;
  // Init
  register_mcp_keys();
  gpiofd = wiringPiSetup();

  if ((q2w = wiringPiI2CSetup (0x20)) == -1)
    { fprintf (stderr, "q2w: Unable to initialise I2C: %s\n", strerror (errno)) ; return 1 ; }

  // Ensure IOCON is in its reset mode. Specifically, the Bank bit (MSB) should be 0. Otherwise registers table is defined differently.
  wiringPiI2CWriteReg8 (q2w, GPINTENB, 0x00); // If BANK == 1, then IOCON_idx == GPINTENB_idx
  wiringPiI2CWriteReg8 (q2w, IOCON, 0x00);

  // Enable appropriate interrupt pins
  wiringPiI2CWriteReg8 (q2w, GPINTENA, mcp[0].inmask);
  wiringPiI2CWriteReg8 (q2w, GPINTENB, mcp[1].inmask);

  wiringPiI2CWriteReg8 (q2w, INTCONA, mcp[0].inmask);
  wiringPiI2CWriteReg8 (q2w, INTCONB, mcp[1].inmask);

  wiringPiI2CWriteReg8 (q2w, DEFVALA, mcp[0].inmask);
  wiringPiI2CWriteReg8 (q2w, DEFVALB, mcp[1].inmask);

  // Ensure GPIOs are set to 'input'
  wiringPiI2CWriteReg8 (q2w, IODIRA, mcp[0].inmask);
  wiringPiI2CWriteReg8 (q2w, IODIRB, mcp[1].inmask);
  // Enable pull-up resistors
  wiringPiI2CWriteReg8 (q2w, GPPUA, mcp[0].inmask);
  wiringPiI2CWriteReg8 (q2w, GPPUB, mcp[1].inmask);
  // Don't reverse polarity
  wiringPiI2CWriteReg8 (q2w, IPOLA, 0x00);
  wiringPiI2CWriteReg8 (q2w, IPOLB, 0x00);
  // Init GPIOs to read 'low'
  wiringPiI2CWriteReg8 (q2w, GPIOA, 0x00);
  wiringPiI2CWriteReg8 (q2w, GPIOB, 0x00);

  if(wiringPiISR(BUTTON_PIN, INT_EDGE_FALLING, &mcp_interrupt_handler) < 0 )
  {
    fprintf(stderr, "Could not setup ISR: %s\n", strerror(errno));
    return 1;
  }

  for (;;)
  {
//    printf ("Waiting ... ") ; fflush (stdout) ;
	}
  // Poll
/*
  int byte_a;
  int byte_b;
  for (;;)
  {
    byte_a = wiringPiI2CReadReg8 (q2w, GPIOA);
    byte_b = wiringPiI2CReadReg8 (q2w, GPIOA);
    printf("a=%d, b=%d\n", byte_a, byte_b);
    delay (500);
  }
  */
  return 0 ;
}