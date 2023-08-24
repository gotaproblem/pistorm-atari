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
#include "../m68k.h"
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


uint8_t fc;

#define CHECK_IRQ(x) (!(x & 0x02) ) //1 << PIN_IPL_ZERO
#define CHECK_BERR(x) (!(x & 0x20) )
#define CHECK_PIN_IN_PROGRESS(x) ((x & 0x01))


static void setup_io() 
{
  int fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC );

  if (fd < 0) {
    printf("Unable to open /dev/mem. Run as root using sudo?\n");
    exit(-1);
  }

  void *gpio_map = mmap ( 
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

#define PLLC        5 /* ARM CLOCK */
#define PLLD        6 /* CORE CLOCK - this should be used for a stable 200 MHz PI_CLK as it is NOT affected by overclocking CPU */

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
#define PLL         PLLC
#define PLL_DIVISOR PLLC_200MHZ //PLLD_25MHZ

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
}


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
#define TXN_END 0x00ffffec
//#define TXN_END (0x00ffff00 | (1 << PIN_WR) | (1 << PIN_RD) | (REG_ADDR_HI << PIN_A0) | (REG_ADDR_LO << PIN_A0))


volatile int g_irq;
volatile int g_buserr;


#ifdef STATS
t_stats RWstats;
#endif


#define CMD_REG_DATA  (REG_DATA << PIN_A0)
#define CMD_ADDR_LO   (REG_ADDR_LO << PIN_A0)
#define CMD_ADDR_HI   (REG_ADDR_HI << PIN_A0)

#define NOP asm("nop")


inline
void ps_write_16 ( uint32_t address, uint16_t data ) 
{
  static uint32_t l;

  g_irq       = 0;

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ( data << 8 ) | CMD_REG_DATA;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END; 

  *(gpio + 7) = ((address & 0xffff) << 8) | CMD_ADDR_LO;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = ( ( (fc << 13) | (address >> 16) ) << 8 ) | CMD_ADDR_HI;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;
  
  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  /* wait for firmware to signal txn done */
	while ( ( l = *(gpio + 13) ) & 1 )
    if ( g_buserr = CHECK_BERR (l) )
      break;
    ;

  g_irq = CHECK_IRQ (l);
  //g_buserr = CHECK_BERR (l);

#ifdef STATS
  RWstats.w16++;  
#endif
}


inline
void ps_write_8 ( uint32_t address, uint16_t data ) 
{
  static uint32_t l;

  if ((address & 1) == 0)
    data <<= 8;

  else
    data &= 0xff;

  g_irq       = 0;

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = data << 8 | CMD_REG_DATA;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = ( (address & 0xffff) << 8) | CMD_ADDR_LO;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = (( (fc<<13) | 0x0100 | (address >> 16) ) << 8) | CMD_ADDR_HI;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

	while ( ( l = *(gpio + 13) ) & 1 )
    if ( g_buserr = CHECK_BERR (l) )
      break;
    ;

  g_irq = CHECK_IRQ (l);
  //g_buserr = CHECK_BERR (l);

#ifdef STATS
  RWstats.w8++;
#endif
}


inline
void ps_write_32 ( uint32_t address, uint32_t value )  
{
  ps_write_16 ( address, value >> 16 );
  ps_write_16 ( address + 2, value );
}


inline
uint16_t ps_read_16 ( uint32_t address ) 
{
	static uint32_t l;

  g_irq       = 0;

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ( (address & 0xffff) << 8 ) | CMD_ADDR_LO;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = ( ( (fc << 13) | 0x0200 | (address >> 16) ) << 8 ) | CMD_ADDR_HI;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0) | (1 << PIN_RD);

  while ( ( l = *(gpio + 13) ) & (1 << PIN_TXN_IN_PROGRESS) )
    ;

 	*(gpio + 10) = TXN_END;

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

#ifdef STATS
  RWstats.r16++;
#endif

  return (l >> 8);
}

inline
uint8_t ps_read_8 ( uint32_t address ) 
{
  static uint32_t l;
  
  g_irq       = 0;

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ( (address & 0xffff)  << 8 )| CMD_ADDR_LO;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 7) = (( (fc << 13) | 0x0300 | (address >> 16) ) << 8) | CMD_ADDR_HI;// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = TXN_END;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0) | (1 << PIN_RD);

  while ( ( l = *(gpio + 13) ) & (1 << PIN_TXN_IN_PROGRESS) )
    ;

  *(gpio + 10) = TXN_END;

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

#ifdef STATS
  RWstats.r8++;
#endif

  if ( (address & 1) == 0 )
    return (l >> 16);

  else
    return (l >> 8);
}


inline
uint32_t ps_read_32 ( uint32_t address ) 
{
  return ( ps_read_16 ( address ) << 16 ) | ps_read_16 ( address + 2 );
}

inline
void ps_write_status_reg(unsigned int value) 
{
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((value & 0xffff) << 8) | (REG_STATUS << PIN_A0);// | (1 << PIN_WR);
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

inline
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

  return value >> 8;
}


void ps_reset_state_machine () 
{
  ps_write_status_reg ( STATUS_BIT_INIT );
  usleep ( 1500 );
  ps_write_status_reg ( 0 );
  usleep ( 100 );
}

void ps_pulse_reset () 
{
  ps_write_status_reg ( 0 );
  usleep ( 100000 );
  ps_write_status_reg ( STATUS_BIT_RESET );
}