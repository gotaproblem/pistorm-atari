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
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include "ps_protocol.h"
#include "../m68k.h"
#include <stdbool.h>


#define CHECK_IRQ(x) (!(x & 0x02) )
#define CHECK_BERR(x) (!(x & 0x20) )
#define TXN_END 0xFFFFCC // 0xffffec
#define PIN_BERR PIN_RESET

#ifdef STATS
t_stats RWstats;
#endif
volatile uint16_t g_irq;
volatile uint32_t g_buserr;
volatile uint32_t *gpio;
volatile uint32_t *gpclk;
volatile uint32_t *gpioBASE;
//volatile bool PS_LOCK;
uint8_t fc;

void (*callback_berr) (uint16_t status, uint32_t address, int mode) = NULL;


void set_berr_callback ( void(*ptr) (uint16_t,uint32_t,int) ) 
{
	callback_berr = ptr;
}


static void setup_io() 
{
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


/* cryptodad */
/* PI_CLK (GPIO 4) definitions */
#define PLLC 5 /* ARM CLOCK  - cpu_freq */
#define PLLD 6 /* CORE CLOCK - gpu_freq this should be used for a stable 200 MHz PI_CLK as it is NOT affected by overclocking CPU */

/* pi3 BCM 2837 */
/* GPIO max clock is 200 MHz */
/* PLLD GPU_FREQ (500 MHz) as defined in /boot/config.txt */

/* Pi4 BCM 2711 */
/* GPIO max clock is 125 MHz */
/* PLLD GPU_FREQ (750 MHz) as defined in /boot/config.txt */

/* max target frequency for PI_CLK (MHz) */

#ifndef PI3 
  /* THIS IS FOR PI4 */
  #define MAX_PI_CLK 125
  #define PLL_TO_USE PLLD

#else 
  /* THIS IS FOR PI3 */
  #define MAX_PI_CLK 200
  #define PLL_TO_USE PLLC

#endif


/* Enable PI_CLK output on GPIO4, adjust divider and pll source depending on pi model */
static void setup_gpclk ( int targetF ) 
{
  int cpuf, coref;
  int div_i;
  int div_f;
  int clk;
  float div;
  FILE *fp;
  char junk[80];
  char *ptr;

  fp = popen ( "vcgencmd measure_clock arm", "r" );
  fgets ( junk, sizeof (junk), fp );
  pclose ( fp );

  ptr = strchr ( junk, '=' );
  sscanf ( (ptr + 1), "%d", &cpuf );
  cpuf /= 1000000;

  fp = popen ( "vcgencmd measure_clock core", "r" );
  fgets ( junk, sizeof (junk), fp );
  pclose ( fp );

  ptr = strchr ( junk, '=' );
  sscanf ( (ptr + 1), "%d", &coref );
  coref /= 1000000;

  printf ( "[INIT] CPU clock is %d MHz\n", cpuf );
  printf ( "[INIT] CORE clock is %d MHz\n", coref );
  
#if (1)
  clk = PLL_TO_USE == PLLC ? cpuf : coref;

  if ( targetF > MAX_PI_CLK )
  {
    targetF = MAX_PI_CLK;
    //printf ( "[INIT] core freq clock was set too high for the PI model - using default of %d\n", MAX_PI_CLK );
  }

  div = clk / (float)targetF;
  div_i = (int)div;
  div_f = (int)( (div - div_i) * 4096 );
  //printf ( "div = %f -> divi %d, divf %d\n", div, div_i, div_f );

  printf ( "[INIT] Using clock divisor %.3f with PLL%c\n", div, PLL_TO_USE == PLLC ? 'C' : 'D' );
  printf ( "[INIT] GPIO clock is %.3f MHz\n", clk / div );//PLL_TO_USE == PLLC ? cpuf / div : coref / div );

  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5); /* kill the clock */
  usleep (100);

  while ( ( *(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7) ); /* wait for clock to stop */
  usleep (100);

  /* bits 23:12 integer part of divisor */
  /* bite 11:0 fractional part of divisor */
  /* SOURCE / (DIV_I + DIV_F / 4096) */

  *(gpclk + (CLK_GP0_DIV / 4)) = CLK_PASSWD | (div_i << 12) | (div_f & 0x7ff);  /* set clock divisors */
  usleep (100);

  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | PLL_TO_USE | (1 << 4); /* set PLL to use 6=plld, 5=pllc and start clock */
  usleep (100);

  while ( ( ( *(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7) ) == 0 ); /* wait for clock to start */
  usleep (100);

#else
int pll = *(gpclk + (CLK_GP0_CTL / 4)) & 0x0f;
printf ( "[INIT] Using PLL%c\n", pll == 4 ? 'A' : pll == 5 ? 'C' : pll == 6 ? 'D' : 'x' );
#endif

  SET_GPIO_ALT (PIN_CLK, 0);  // assign clock to gpio pin 4 (PI_CLK)
}


void ps_setup_protocol ( int targetF ) 
{
  setup_io ();
  setup_gpclk ( targetF );

  gpio [10] = 0xffffec;

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;
}


inline
void ps_write_16 ( uint32_t address, uint16_t data )
{
  static uint32_t l;

#if (1)
  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ( data << 8 );// | 0x80;//CMD_REG_DATA | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( (address & 0xffff) << 8 ) | 0x04;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( ( (fc << 13) | (address >> 16) ) << 8 ) | 0x08;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END; 
  
  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

	while ( ( l = gpio [13] ) & 1 ); // wait for firmware to signal transaction completed

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

#ifdef STATS
  RWstats.w16++;  
#endif

#else
  GPFSEL_OUTPUT;

  GPIO_WRITEREG ( REG_DATA, data );//(data & 0xFFFF) );
  GPIO_WRITEREG ( REG_ADDR_LO, (address & 0xFFFF) );
  GPIO_WRITEREG ( REG_ADDR_HI, ( (fc << 13) | 0x0000 | (address >> 16)) );

  GPFSEL_INPUT;

  WAIT_TXN;

  l = gpio [13];
  g_buserr = CHECK_BERR (l);
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

#if (1)
  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = data << 8;// | 0x80;//CMD_REG_DATA | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ((address & 0xffff) << 8) | 0x04;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = (( (fc<<13) | 0x0100 | (address >> 16) ) << 8) | 0x08;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;
  
	while ( ( l = gpio [13] ) & 1 );

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);
  
#ifdef STATS
  RWstats.w8++;
#endif

#else
  GPFSEL_OUTPUT;

  GPIO_WRITEREG ( REG_DATA, data );//(data & 0xFFFF) );
  GPIO_WRITEREG ( REG_ADDR_LO, (address & 0xFFFF) );
  GPIO_WRITEREG ( REG_ADDR_HI, ( (fc << 13) | 0x0100 | (address >> 16)) );

  GPFSEL_INPUT;

  WAIT_TXN;

  l = gpio [13];
  g_buserr = CHECK_BERR (l);
#endif
}


inline
void ps_write_32 ( uint32_t address, uint32_t value ) 
{
  static uint32_t l;

#if (1)
  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ( (value >> 16) << 8 );// | 0x80;//CMD_REG_DATA | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( (address & 0xffff) << 8 ) | 0x04;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( ( (fc << 13) | (address >> 16) ) << 8 ) | 0x08;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END; 
  
  //gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  //gpio [2] = GPFSEL2_INPUT;

	while ( gpio [13] & 1 ); // wait for firmware to signal transaction completed


  address += 2;

  //gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  //gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ( value << 8 );// | 0x80;//CMD_REG_DATA | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( (address & 0xffff) << 8 ) | 0x04;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( ( (fc << 13) | (address >> 16) ) << 8 ) | 0x08;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END; 
  
  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

	while ( ( l = gpio [13] ) & 1 ); // wait for firmware to signal transaction completed


  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);
#else
  GPFSEL_OUTPUT;

  GPIO_WRITEREG ( REG_DATA, ( (value >> 16) & 0xFFFF) );
  GPIO_WRITEREG ( REG_ADDR_LO, (address & 0xFFFF) );
  GPIO_WRITEREG ( REG_ADDR_HI, ( (fc << 13) | 0x0000 | (address >> 16)) );

  //GPFSEL_INPUT;

  WAIT_TXN;

  address += 2;

  //GPFSEL_OUTPUT;

  GPIO_WRITEREG ( REG_DATA, ( value & 0xFFFF) );
  GPIO_WRITEREG ( REG_ADDR_LO, (address & 0xFFFF) );
  GPIO_WRITEREG ( REG_ADDR_HI, ( (fc << 13) | 0x0000 | (address >> 16)) );

  GPFSEL_INPUT;

  WAIT_TXN;

  l = gpio [13];
  g_buserr = CHECK_BERR (l);
#endif
}


inline
uint16_t ps_read_16 ( uint32_t address ) 
{
	static uint32_t l;

  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ( (address & 0xffff) << 8 ) | 0x04;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( ( (fc << 13) | 0x0200 | (address >> 16)  ) << 8 ) | 0x08;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = 0x40;//(REG_DATA << PIN_A0) | (1 << PIN_RD);

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

  while ( ( l = gpio [13] ) & 1 )
    ;
  
#ifdef PI3
  l = gpio [13];
#endif

 	gpio [10] = TXN_END;

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

  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ((address & 0xffff) << 8) | 0x04;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = (( (fc << 13) | 0x0300 | (address >> 16) ) << 8) | 0x08;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = 0x40;//(REG_DATA << PIN_A0) | (1 << PIN_RD);

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;
  
  while ( ( l = gpio [13] ) & 1 )
    ;

#ifdef PI3
  l = gpio [13];
#endif

  gpio [10] = TXN_END;
  
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
#if (0)
  return ( ps_read_16 ( address ) << 16 ) | ps_read_16 ( address + 2 );

#else
  static uint32_t l;
  static uint32_t d;

  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ( (address & 0xffff) << 8 ) | 0x04;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( ( (fc << 13) | 0x0200 | (address >> 16)  ) << 8 ) | 0x08;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = 0x40;//(REG_DATA << PIN_A0) | (1 << PIN_RD);

  //gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  //gpio [2] = GPFSEL2_INPUT;

  while ( gpio [13] & 1 );
  
  l = gpio [13];

 	gpio [10] = TXN_END;

  d = (l >> 8) << 16;


  address += 2;

  //gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  //gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ( (address & 0xffff) << 8 ) | 0x04;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( ( (fc << 13) | 0x0200 | (address >> 16)  ) << 8 ) | 0x08;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = 0x40;//(REG_DATA << PIN_A0) | (1 << PIN_RD);

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

  while ( gpio [13] & 1 );
  
  l = gpio [13];

 	gpio [10] = TXN_END;


  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

#ifdef STATS
  RWstats.r16++;
#endif

return d | ( (l >> 8) & 0xFFFF );
#endif
}


inline
void ps_write_status_reg ( unsigned int value ) 
{
  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ((value & 0xffff) << 8) | 0x8C;//(REG_STATUS << PIN_A0) | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

#ifdef STATS
  RWstats.wstatus++;
#endif
}


inline
uint16_t ps_read_status_reg () 
{
  static uint32_t l;

  while ( gpio [13] & 1 );

  gpio [7] = 0x4C; //(REG_STATUS << PIN_A0) | (1 << PIN_RD);

  while ( gpio [13] & 1 );

  l = gpio [13];

  gpio [10] = TXN_END;

#ifdef STATS
  RWstats.rstatus++;
#endif

  return (l >> 8);
}


inline
void ps_reset_state_machine () 
{
  ps_write_status_reg ( STATUS_BIT_INIT );
  usleep(16);
  ps_write_status_reg ( 0 );
  usleep(16);
}


/* pulse the reset line - assert for 100ms */
inline
void ps_pulse_reset () 
{
  ps_write_status_reg ( STATUS_BIT_RESET );
  usleep(16);
  ps_write_status_reg ( 0 );
  usleep(16);
}