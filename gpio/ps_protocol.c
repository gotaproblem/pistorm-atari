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
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <pthread.h>
#include "ps_protocol.h"
#include "../m68k.h"
#include <stdbool.h>


volatile unsigned int *gpio;
volatile unsigned int *gpclk;
volatile uint32_t *gpioBASE;
volatile bool PS_LOCK;

unsigned int gpfsel0;
unsigned int gpfsel1;
unsigned int gpfsel2;

unsigned int gpfsel0_o;
unsigned int gpfsel1_o;
unsigned int gpfsel2_o;



void (*callback_berr) (uint16_t status, uint32_t address, int mode) = NULL;

uint8_t fc;

#define CHECK_IRQ(x) (!(x & 0x02) )
#define CHECK_BERR(x) (!(x & 0x20) )
//#define CHECK_BERR(x) (x & 0x20) /* returns 0x20 if bus -error */
//#define CHECK_PIN_IN_PROGRESS(x) ((x & 0x01))

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
  #define PLL_TO_USE PLLD

#endif


/* Enable PI_CLK output on GPIO4, adjust divider and pll source depending on pi model */
static void setup_gpclk() 
{
  int cpuf, coref;
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

  int clk = PLL_TO_USE == PLLC ? cpuf : coref;
  int div = clk / MAX_PI_CLK;

  if ( div * MAX_PI_CLK != clk )
    div += 1;

//div = 10;
  printf ( "[INIT] CPU clock is %d MHz\n", cpuf );
  printf ( "[INIT] CORE clock is %d MHz\n", coref );
  printf ( "[INIT] Using clock divisor %d with PLL%c\n", div, PLL_TO_USE == PLLC ? 'C' : 'D' );
  printf ( "[INIT] GPIO clock is %d MHz\n", PLL_TO_USE == PLLC ? cpuf / div : coref / div );

  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5);
  usleep (30);

  while ( (*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7) );
  usleep (100);

  *(gpclk + (CLK_GP0_DIV / 4)) = CLK_PASSWD | (div << 12);  // bits 23:12 integer part of divisor
  usleep (30);

  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | PLL_TO_USE | (1 << 4); // 6=plld, 5=pllc
  usleep (30);

  while ( ((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0 );
  usleep (100);

  SET_GPIO_ALT (PIN_CLK, 0);  // assign clock to gpio pin 4 (PI_CLK)
}


void ps_setup_protocol () 
{
  setup_io ();
  setup_gpclk ();

  gpio [10] = 0xffffec;

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;
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
#define TXN_END 0xffffec
#define PIN_BERR PIN_RESET


volatile int g_irq;
volatile uint32_t g_buserr;


#ifdef STATS
t_stats RWstats;
#endif



inline
void ps_write_16 ( uint32_t address, uint16_t data )
{
  static uint32_t l;

  while ( PS_LOCK );
  //while ( gpio [13] & 1 );

  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ( data << 8 ) | 0x80;//CMD_REG_DATA | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( (address & 0xffff) << 8 ) | 0x84;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( ( (fc << 13) | (address >> 16) ) << 8 ) | 0x88;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END; 
  
  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

	while ( ( l = gpio [13] ) & 1 ) // wait for firmware to signal transaction completed
    ;

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

#ifdef STATS
  RWstats.w16++;  
#endif
}


inline
void ps_write_8 ( uint32_t address, uint16_t data ) 
{
  static uint32_t l;

  while ( PS_LOCK );
  //while ( gpio [13] & 1 );

  if ((address & 1) == 0)
    data <<= 8;

  else
    data &= 0xff;

  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = data << 8 | 0x80;//CMD_REG_DATA | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ((address & 0xffff) << 8) | 0x84;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = (( (fc<<13) | 0x0100 | (address >> 16) ) << 8) | 0x88;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;
  
	while ( ( l = gpio [13] ) & 1 )
    ;

  g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);
  
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
#ifdef PI3
  static uint32_t value;
#endif

  while ( PS_LOCK );
  //while ( gpio [13] & 1 );

  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ( (address & 0xffff) << 8 ) | 0x84;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = ( ( (fc << 13) | 0x0200 | (address >> 16)  ) << 8 ) | 0x88;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

  gpio [7] = 0x40;//(REG_DATA << PIN_A0) | (1 << PIN_RD);

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
#ifdef PI3
  static uint32_t value;
#endif
  
  while ( PS_LOCK );
  //while ( gpio [13] & 1 );

  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  gpio [7] = ((address & 0xffff) << 8) | 0x84;//CMD_ADDR_LO | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [7] = (( (fc << 13) | 0x0300 | (address >> 16) ) << 8) | 0x88;//CMD_ADDR_HI | (1 << PIN_WR);
  gpio [7] = 0x80;
  gpio [10] = 0x80;
  gpio [10] = TXN_END;

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

  gpio [7] = 0x40;//(REG_DATA << PIN_A0) | (1 << PIN_RD);
  
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


uint32_t ps_read_32 ( uint32_t address ) 
{
  return ( ps_read_16 ( address ) << 16 ) | ps_read_16 ( address + 2 );
}


void ps_write_status_reg ( unsigned int value ) 
{
  while ( PS_LOCK );

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


uint16_t ps_read_status_reg () 
{
  static uint32_t value;

  while ( PS_LOCK );
  while ( gpio [13] & 1 );

  gpio [7] = 0x4C; //(REG_STATUS << PIN_A0) | (1 << PIN_RD);

  while ( gpio [13] & 1 )
    ;

  value = gpio [13];

  gpio [10] = TXN_END;

#ifdef STATS
  RWstats.rstatus++;
#endif

  return value >> 8;
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