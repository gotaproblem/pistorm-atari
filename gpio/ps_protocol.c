// SPDX-License-Identifier: MIT

/*
  Original Copyright 2020 Claude Schwarz
  Code reorganized and rewritten by
  Niklas Ekström 2021 (https://github.com/niklasekstrom)
*/

//#define DEBUG

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include "ps_protocol.h"
#include "m68k.h"
//#include "bcm2835.h"


volatile unsigned int *gpio;
volatile unsigned int *gpclk;
volatile uint32_t *gpioBASE;

unsigned int gpfsel0;
unsigned int gpfsel1;
unsigned int gpfsel2;

unsigned int gpfsel0_o;
unsigned int gpfsel1_o;
unsigned int gpfsel2_o;

//int g_irq;
void ps_berrInit ( void );

void (*callback_berr) (uint16_t status, uint32_t address, int mode) = NULL;

uint8_t fc;

#define CHECK_IRQ(x) (!(x & 0x02) ) //1 << PIN_IPL_ZERO
#define CHECK_BERR(x) (!(x & 0x20) )
#define CHECK_PIN_IN_PROGRESS(x) ((x & 0x01))

void set_berr_callback ( void(*ptr) (uint16_t,uint32_t,int) ) 
{
	callback_berr = ptr;
}

static void setup_io() 
{
  //if ( ! bcm2835_init () )
  //{
  //    printf ( "%s bcm_init () failed\n", __func__ );
  //    return;
  //}

  int fd = open("/dev/mem", O_RDWR | O_SYNC);
 if (fd < 0) {
    printf("Unable to open /dev/mem. Run as root using sudo?\n");
    exit(-1);
  }

  void *gpio_map = mmap(
      NULL,                    // Any adddress in our space will do
      BCM2708_PERI_SIZE,       // Map length
      PROT_READ | PROT_WRITE,  // Enable reading & writting to mapped memory
      MAP_SHARED,              // Shared with other processes
      fd,                      // File to map
      BCM2708_PERI_BASE        // Offset to GPIO peripheral
  );

  close(fd);

  if (gpio_map == MAP_FAILED) {
    printf("mmap failed, errno = %d\n", errno);
    exit(-1);
  }

  gpio = ((volatile unsigned *)gpio_map) + GPIO_ADDR / 4;
  gpclk = ((volatile unsigned *)gpio_map) + GPCLK_ADDR / 4;
}

/* cryptodad */
/* PLLC ARM CLOCK typically 1.2 to 1.4 GHz */
/* PLLD CORE CLOCK typically 400 MHz */

#define PLLC 5 /* ARM CLOCK */
#define PLLD 6 /* CORE CLOCK - this should be used for a stable 200 MHz PI_CLK as it is NOT affected by overclocking CPU */

#define PLLC_200MHZ 6  /* ARM clock / 6 eg. 1200 MHz / 6 = 200 MHz*/
#define PLLC_100MHZ 7  /* ARM clock / 7 = 100 MHz */
#define PLLC_50MHZ  8  /* ARM clock / 8 = 50 MHz */
#define PLLC_25MHZ  9  /* ARM clock / 8 = 25 MHz */
#define PLLC_12MHZ  10
#define PLLC_6MHZ   25
 
#define PLLD_200MHZ 2  /* CORE clock / 2 eg. 400 MHz / 2 = 200 MHz */
#define PLLD_100MHZ 3  /* CORE clock / 4   */
#define PLLD_50MHZ  4  /* CORE clock / 8   */
#define PLLD_25MHZ  5  /* CORE clock / 16  */
#define PLLD_12MHZ  6  /* CORE clock / 32  */
#define PLLD_6MHZ   7  /* CORE clock / 64  */
#define PLLD_3MHZ   8  /* CORE clock / 128 */
#define PLLD_2MHZ   9  /* CORE clock / 256 */
#define PLLD_1MHZ   10 /* CORE clock / 512 */

/* change these 2 settings to configure clock */
#define PLL PLLD
#define PLL_DIVISOR PLLD_25MHZ

static void setup_gpclk() 
{
  // Enable 200MHz CLK output on GPIO4, adjust divider and pll source depending
  // on pi model
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5);
  usleep(30);

  while ((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7));
  usleep(100);

  *(gpclk + (CLK_GP0_DIV / 4)) = CLK_PASSWD | (PLL_DIVISOR << 12);  // divider , 6=200MHz on pi3
  usleep(30);

  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | PLL | (1 << 4);
  //*(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | PLL;  // pll? 6=plld, 5=pllc
  usleep(30);

  while (((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0)    ;
  usleep(100);

  SET_GPIO_ALT(PIN_CLK, 0);  // assign clock to this gpio pin (PI_CLK)
}

void ps_setup_protocol () 
{
  setup_io();
  setup_gpclk();

  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  /* cryptodad */
  //ps_berrInit ();

  //bcm2835_init ();
  //gpioBASE = bcm2835_regbase ( BCM2835_REGBASE_GPIO );

  //printf ( "gpio = 0x%08x, gpioBASE = 0x%08x\n", gpio, gpioBASE );
}

#if (0)
/* cryptodad TODO - why is this extra check needed if _PI_RESET is already signalling a BERR ??? */
/* cryptodad 6th April 2023 if this status check is not performed, can get segmentation faults */
void check_berr ( uint32_t address, int mode ) 
{
  uint16_t status = ps_read_status_reg ();

  if ( (status & STATUS_BIT_BERR) && callback_berr )  
      callback_berr ( status, address, mode ); 
  //callback_berr ( STATUS_BIT_BERR, address, mode ); 
}
#endif


/* cryptodad */

/* 
GPIO 
gpio + 0  = GPFSEL0 GPIO Function Select 0
gpio + 1  = GPFSEL1 GPIO Function Select 1
gpio + 2  = GPFSEL2 GPIO Function Select 2
gpio + 7  = GPSET   GPIO Pin Output Set 0
gpio + 10 = GPCLR   GPIO Pin Output Clear 0
gpio + 13 = GPLEV   GPIO Pin Levl 0
gpio + 16 = GPEDS   GPIO Pin Event Detect Status 0
gpio + 19 = GPREN   GPIO Pin Rising Edge Detect Enable 0
gpio + 22 = GPFEN   GPIO Pin Falling Edge Detect Enable 0
gpio + 25 = GPHEN   GPIO Pin High Detect Enable 0
gpio + 28 = GPLEN   GPIO Pin Low Detect Enable 0
gpio + 31 = GPAREN  GPIO Pin Asysnchronous Rising Edge Detect Enable 0
gpio + 34 = GPAFEN  GPIO Pin Asysnchronous Falling Edge Detect Enable 0
*/
//#define TXN_END 0xffffec
#define TXN_END (0xffffff00 | (1 << PIN_WR) | (1 << PIN_RD) | (REG_ADDR_HI << PIN_A0) | (REG_ADDR_LO << PIN_A0))

#define BERR_POLL 1
#define PIN_BERR PIN_RESET
#define ARM_BERR_INT 

//extern void call_berr ( uint16_t, uint32_t, uint );
volatile int g_irq;
volatile int g_buserr;
volatile int g_beAddress;
volatile int g_beMode;

#ifdef STATS
t_stats RWstats;
#endif

void ps_berrInit ( void )
{
  uint32_t *gpioPtr = (uint32_t *)(gpio + 16);
  //uint32_t gpioStatus = *gpioPtr;

  *(gpio + 34) = ( 1 << PIN_BERR ); // enable asynchronous falling edge detection for bus-error
  //*(gpio + 22) = ( 1 << PIN_TXN_IN_PROGRESS ); // enable asynchronous falling edge detection for txn
  //*(gpio + 34) = ( 1 << PIN_BERR ) | ( 1 << PIN_TXN_IN_PROGRESS );

  *gpioPtr = ( 1 << PIN_BERR ); // clear edge detection
  //*gpioPtr = ( 1 << PIN_TXN_IN_PROGRESS ); // clear edge detection
  //*gpioPtr = ( 1 << PIN_BERR ) | ( 1 << PIN_TXN_IN_PROGRESS );
}

void ps_berrIAK ( uint32_t address, int mode )
{
  uint32_t l;

  while ( ( l = *(gpio + 13) ) & (1 << PIN_TXN_IN_PROGRESS) )
    ;

  if ( mode == 1 )
    *(gpio + 10) = TXN_END;

  if ( CHECK_BERR(l) )// do we have a bus error?
  {
    //check_berr ( address, mode );
  }
}

#define OLDWAY
//#define CMD_SET (1 << 7)
//#define CMD_READ 1
//#define CMD_WRITE 2
//#define CMD_CLR (1 << 7)
//#define TXN_END 0xffffec //(0xffff00 | (1 << PIN_WR) | (1 << PIN_RD) | (1 << PIN_RESET) | (REG_ADDR_HI << PIN_A0) | (REG_ADDR_LO << PIN_A0))
//#define TXN_END (0xffffff00 | (1 << PIN_WR) | (1 << PIN_RD) | (REG_ADDR_HI << PIN_A0) | (REG_ADDR_LO << PIN_A0))

#define CMD_REG_DATA  (REG_DATA << PIN_A0)
#define CMD_ADDR_LO   (REG_ADDR_LO << PIN_A0)
#define CMD_ADDR_HI   (REG_ADDR_HI << PIN_A0)

#define NOP asm("nop") // asm("nop"); asm("nop"); asm("nop");


inline
//void ps_write_16 ( uint32_t address, uint16_t data )
void ps_write_16 ( t_a32 address, uint16_t data ) 
{
  static uint32_t l;

  //while ( *(gpio + 13) & 1 );
  //g_irq = 0;

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

#ifdef OLDWAY
  *(gpio + 7) = ( data << 8 ) | CMD_REG_DATA | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = ( address.lo << 8 ) | CMD_ADDR_LO | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = ( ( (fc << 13) | address.hi ) << 8 ) | CMD_ADDR_HI | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;
#else
  *(gpio + 7) = CMD_SET;
  *(gpio + 7) = packet [0];
  *(gpio + 10) = CMD_CLR;
  *(gpio + 7) = CMD_SET;
  *(gpio + 7) = packet [1];
  *(gpio + 10) = CMD_CLR;
#endif  
  
  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

#if BERR_POLL
	while ( ( l = *(gpio + 13) ) & 1 ) // wait for firmware to signal command completed
    ;

  //if ( CHECK_IRQ (l) )
  //  g_irq = 1;

	//if ( CHECK_BERR (l) )
  //  g_buserr = 1;

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);
#else
  ps_berrIAK ( address, 0 );
#endif

#ifdef STATS
  RWstats.w16++;  
#endif
}


inline
void ps_write_8 ( t_a32 address, uint16_t data ) 
//void ps_write_8 ( uint32_t address, uint16_t data ) 
{
  static uint32_t l;
  //uint16_t alo = address;// & 0xffff;
  //uint16_t ahi = address >> 16;

  //while ( *(gpio + 13) & 1 );
  //g_irq = 0;

  if ((address.lo & 1) == 0)
    data <<= 8;

  else
    data &= 0xff;

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = data << 8 | CMD_REG_DATA | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = (address.lo << 8) | CMD_ADDR_LO | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = (( (fc<<13) | 0x0100 | address.hi) << 8) | CMD_ADDR_HI | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

#if BERR_POLL
	while ( ( l = *(gpio + 13) ) & 1 )
    ;

  //if ( CHECK_IRQ (l) )
  //  g_irq = 1;

	//if ( CHECK_BERR (l) )
		//check_berr ( address, 0 );
  //  g_buserr = 1;

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);
#else
  ps_berrIAK ( address, 0 );
#endif

#ifdef STATS
  RWstats.w8++;
#endif
}


inline
void ps_write_32 ( t_a32 address, uint32_t value )  
//void ps_write_32 ( uint32_t address, uint32_t value ) 
{
  ps_write_16 ( address, value >> 16 );
  ps_write_16 ( (t_a32)(address.address + 2), value );
}

//#define NOP asm("nop"); asm("nop");


inline
//uint16_t ps_read_16 ( uint32_t address ) 
uint16_t ps_read_16 ( t_a32 address ) 
{
	static uint32_t l;

  //while ( *(gpio + 13) & 1 );
 // g_irq = 0;

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ( address.lo << 8 ) | CMD_ADDR_LO | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = ( ( (fc << 13) | 0x0200 | address.hi ) << 8 ) | CMD_ADDR_HI | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0) | (1 << PIN_RD);

#if BERR_POLL
  while ( ( l = *(gpio + 13) ) & (1 << PIN_TXN_IN_PROGRESS) )
    ;

 	*(gpio + 10) = TXN_END;

  //if ( CHECK_IRQ (l) )
  //  g_irq = 1;

	//if ( CHECK_BERR (l) )
  //  g_buserr = 1;

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

#else
  ps_berrIAK ( address, 1 );
#endif

#ifdef STATS
  RWstats.r16++;
#endif

  return (l >> 8);
}

inline
uint8_t ps_read_8 ( t_a32 address ) 
//uint8_t ps_read_8 ( uint32_t address ) 
{
  static uint32_t l;
  //uint16_t alo = address;// & 0xffff;
  //uint16_t ahi = address >> 16;

  //while ( *(gpio + 13) & 1 );
  //g_irq = 0;
  
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = (address.lo << 8) | CMD_ADDR_LO | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = (( (fc << 13) | 0x0300 | address.hi) << 8) | CMD_ADDR_HI | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0) | (1 << PIN_RD);

#if BERR_POLL
  while ( ( l = *(gpio + 13) ) & (1 << PIN_TXN_IN_PROGRESS) )
    ;

  *(gpio + 10) = TXN_END;

  //if ( CHECK_IRQ (l) )
  //  g_irq = 1;

	//if ( CHECK_BERR (l) )
  //  g_buserr = 1;

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

#else
  ps_berrIAK ( address, 1 );
#endif

#ifdef STATS
  RWstats.r8++;
#endif

  if ( (address.lo & 1) == 0 )
    return (l >> 16);

  else
    return (l >> 8);
}


uint32_t ps_read_32 ( t_a32 address ) 
//uint32_t ps_read_32 ( uint32_t address ) 
{
  return ( ps_read_16 ( address ) << 16 ) | ps_read_16 ( (t_a32)(address.address + 2) );
}


void ps_write_status_reg(unsigned int value) 
{
  //while ( *(gpio + 13) & 1 );

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((value & 0xffff) << 8) | (REG_STATUS << PIN_A0) | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
#ifdef STATS
  RWstats.wstatus++;
#endif
}


uint16_t ps_read_status_reg () 
{
  static uint32_t value;


  while ( *(gpio + 13) & 1 );

  *(gpio + 7) = (REG_STATUS << PIN_A0) | (1 << PIN_RD);

  while ( ( value = *(gpio + 13) ) & (1 << PIN_TXN_IN_PROGRESS) )
    ;

  *(gpio + 10) = TXN_END;

#ifdef STATS
  RWstats.rstatus++;
#endif

  return value >> 8;// & 0xffff;
}


void ps_reset_state_machine () 
{
  ps_write_status_reg(STATUS_BIT_INIT);
  usleep(1500);
  ps_write_status_reg(0);
  usleep(100);
}

void ps_pulse_reset () 
{
  ps_write_status_reg(0);
  usleep(100000);
  ps_write_status_reg(STATUS_BIT_RESET);
}

#if (0)
unsigned int ps_get_ipl_zero() {
  unsigned int value = *(gpio + 13);
  while ((value=*(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {}
  return value & (1 << PIN_IPL_ZERO);
}

#define INT2_ENABLED 1

void ps_update_irq() {
  unsigned int ipl = 0;

  if (!ps_get_ipl_zero()) {
    unsigned int status = ps_read_status_reg();
    ipl = (status & 0xe000) >> 13;
  }

  /*if (ipl < 2 && INT2_ENABLED && emu_int2_req()) {
    ipl = 2;
  }*/

  m68k_set_irq(ipl);
}
#endif