#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <wiringPi.h>
#include <wiringPiI2C.h>
#include <linux/input.h>
#include <time.h>

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
#define ESC_COMBO_PIN_1 14
#define ESC_COMBO_PIN_2 15 // Holding both ESC combo keys will map to ESC
#define COIN_COMBO_PIN_1 1
#define COIN_COMBO_PIN_2 5 // Holding both COIN combo keys will map to 5 which should be set as the coin button

struct {
  int pin; // 0-15, 0-7 is GPIOA, 8-15 is GPIOB
  int key;
} io[] = {
//    Input    Output (from /usr/include/linux/input.h)
  { 0,      KEY_LEFT     },
  {  1,      KEY_DOWN    },
  {  2,      KEY_RIGHT    },
  {  3,      KEY_UP    },
  {  4,      KEY_D    },
  {  5,      KEY_F    },
  {  6,      KEY_G    },
  {  7,      KEY_R    },
  {  14,      KEY_1    },
  {  15,      KEY_2    }

};
#define IOLEN (sizeof(io) / sizeof(io[0])) // io[] table size

// MCP23017 GPIO 8-bit row context
typedef struct{
  int idx; // row idx, A=0, B=1
  int inmask;
  int lastvalue;
  int key_value[N_ROW_PINS]; // -1=unused, 0=low, 1=high
  int key_char[N_ROW_PINS];
} mcp_row;

mcp_row mcp[N_MCP_ROWS];

typedef struct{
  int n_active; // number of concurrent keys required to activate
  int t_delay; // hold threshold in seconds
  int values[N_ROW_PINS];
  time_t start_time;
  int key;
} special_event_context;

special_event_context esc_event;
special_event_context coin_event;

int q2w;

void register_mcp_keys() {  
  int i;
  for (i=0; i<N_MCP_ROWS; i++) {
    mcp[i].inmask = 0;
    memset(mcp[i].key_value, -1, sizeof(mcp[i].key_value));
    memset(mcp[i].key_char, -1, sizeof(mcp[i].key_char));
  }

  for(i=0; i<IOLEN; i++) {
    // printf("Configuring pin %d\n", io[i].pin);
    int idx = (floor(io[i].pin/N_ROW_PINS));
    int pin = io[i].pin % N_ROW_PINS;
    mcp[idx].inmask |= 1<<pin;
    mcp[idx].key_value[pin] = 1; // default is high
    mcp[idx].key_char[pin] = io[i].key;
  }

  for (i=0; i<N_MCP_ROWS; i++) {
    mcp[i].lastvalue = mcp[i].inmask;
  }

  // printf("Row A=%d, Row B=%d\n", mcp[0].inmask, mcp[1].inmask);

}

void register_special_events() {
  esc_event.n_active = 2;
  esc_event.t_delay = 3;
  esc_event.key = KEY_ESC;
  memset(esc_event.values, 0, sizeof(esc_event.values));

  coin_event.n_active = 2;
  coin_event.t_delay = 0;
  coin_event.key = KEY_5;
  memset(coin_event.values, 0, sizeof(coin_event.values));
}

void handle_multi_event(special_event_context *e, int pin1, int pin2, int i, int j, int x, int f) {
  int pinidx, status, prev, next, dt;
  time_t t;
  pinidx = i*N_ROW_PINS + j;
  
  // Change pins and start/stop an event timer
  if ((pinidx == pin1 || pinidx == pin2)) {
    prev = (*e).values[0] + (*e).values[1];
    
    if (pinidx == pin1) {
      (*e).values[0] = x;
    } else {
      (*e).values[1] = x;
    }

    next = (*e).values[0] + (*e).values[1];

    if (prev == 2 && next < 2) {
      // stop event
      (*e).start_time = 0;
    } else if (prev < 2 && next == 2) {
      // start event
      (*e).start_time = time(NULL);
    }
  }

  // Test if event timer has reached threshold
  if ((*e).start_time > 0) {
    t = time(NULL);
    dt = (t - (*e).start_time);
    if (dt >= (*e).t_delay) {
      sendKey((*e).key, x);
      (*e).start_time = 0;
      (*e).values[0] = 0;
      (*e).values[1] = 0;
      // printf("Escape clicked! held thres = %d\n", dt);
    }
  }

}

void handle_coin(int i, int j, int x, int f) {
  handle_multi_event(&coin_event, COIN_COMBO_PIN_1, COIN_COMBO_PIN_2, i, j, x, f);
}

void handle_esc(int i, int j, int x, int f) {
  handle_multi_event(&esc_event, ESC_COMBO_PIN_1, ESC_COMBO_PIN_2, i, j, x, f);
}

void mcp_interrupt_handler (void) { 
struct wiringPiNodeStruct *myNode ;
int val[N_MCP_ROWS], ival[N_MCP_ROWS], xval[N_MCP_ROWS], i, j, x, f;

// read values
val[0] = wiringPiI2CReadReg8 (q2w, GPIOA);
val[1] = wiringPiI2CReadReg8 (q2w, GPIOB);

// collect events
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
   //    printf("Pin %d, x=%d, f=%d \n", j, x, f);
      if (f) {
//          printf("Pin %d changed! - %d, x=%d\n", j, f, x);
        sendKey(mcp[i].key_char[j], x);
      }

      handle_coin(i, j, x, f);
      handle_esc(i, j, x, f);
      delayMicroseconds(4000);
    }
  }
}

}

int main (int argc, char *argv [])
{
  int gpiofd;
  // Init
  register_mcp_keys();
  register_special_events();
  if(init_uinput() == 0){
    sleep(1);
  }

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

  if(wiringPiISR(BUTTON_PIN, INT_EDGE_BOTH, &mcp_interrupt_handler) < 0 )
  {
    fprintf(stderr, "Could not setup ISR: %s\n", strerror(errno));
    return 1;
  }

  for (;;)
  {
    delayMicroseconds(20000);
//    printf ("Waiting ... ") ; fflush (stdout) ;
    wiringPiI2CReadReg8 (q2w, GPIOA);
    wiringPiI2CReadReg8 (q2w, GPIOB);
	}

  return 0 ;
}