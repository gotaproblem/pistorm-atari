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

/* DEBUG stuff */
//#define IO_DEBUG 

//#define CHECK_IRQ(x) (!(x & 0x02) ) // if there is an interrupt, return positive
#define CHECK_IRQ(x) (((x & 0x10) >> 2) | (x & 0x02)) // only using IPL1 and IPL2, these are on GPIO 1 & 4
#define CHECK_BERR(x) (!(x & 0x20) ) // if there is a BERR, return positive

/* clear output bits only */
//#define TXN_END 0xffffff //0xffffde // don't clear bits 5, 0 // 0xffffcc //0xffffec
//#define TXN_END 0xffff8c
#define TXN_END 0xffffcc
//#define PIN_BERR PIN_RESET

#ifdef STATS
t_stats RWstats;
#endif
volatile int g_irq;
volatile uint32_t g_buserr;
volatile uint32_t *gpio;
volatile uint32_t *gpclk;
//volatile uint32_t *gpioBASE;
volatile uint32_t *ioset;
volatile uint32_t *ioclr;
volatile uint32_t *ioread;
uint8_t fc;
volatile int IO; // 0 = write, 1 = read

volatile int g_timeout;
volatile int g_io_done;


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

  ioset  = (volatile uint32_t *)gpio + 7;
  ioclr  = (volatile uint32_t *)gpio + 10;
  ioread = (volatile uint32_t *)gpio + 13;
}

static 
void create_dev_mem_mapping () 
{
  int fd = open ( "/dev/gpiomem", O_RDWR | O_SYNC );
  
  if ( fd < 0 ) 
  {
    printf ( "Unable to open /dev/gpiomem.\n" );
    exit ( -1 );
  }

  void *gpio_map = mmap (
      NULL,                    // Any adddress in our space will do
      (4*1024),		           // Map length
      PROT_READ | PROT_WRITE,  // Enable reading & writting to mapped memory
      MAP_SHARED,              // Shared with other processes
      fd,                      // File to map
      0			                   // Offset to GPIO peripheral
  );

  close(fd);

  if ( gpio_map == MAP_FAILED ) 
  {
    printf ( "mmap failed, errno = %d\n", errno );
    exit ( -1 );
  }

  gpio   = (volatile uint32_t *)gpio_map;
  ioset  = (volatile uint32_t *)gpio + 7;
  ioclr  = (volatile uint32_t *)gpio + 10;
  ioread = (volatile uint32_t *)gpio + 13;

  //gpclk = ((volatile uint32_t *)gpio) + GPCLK_ADDR / 4;
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

  //clk = PLL_TO_USE == PLLC ? cpuf : coref;

  //if ( targetF > MAX_PI_CLK )
  //{
  //  targetF = MAX_PI_CLK;
  //  printf ( "[INIT] core freq clock was set too high for the PI model - using default of %d\n", MAX_PI_CLK );
  //}

  //div = clk / (float)targetF;
  
  //if ( div * MAX_PI_CLK != clk )
  //  div += 1;

  //div_i = (int)div;
  //div_f = (int)( (div - div_i) * 4096 );
  //printf ( "div = %f -> divi %d, divf %d\n", div, div_i, div_f );

  printf ( "[INIT] CPU clock is %d MHz\n", cpuf );
  printf ( "[INIT] CORE clock is %d MHz\n", coref );
  //printf ( "[INIT] Using clock divisor %.3f with PLL%c\n", div, PLL_TO_USE == PLLC ? 'C' : 'D' );
  //printf ( "[INIT] GPIO clock is %.3f MHz\n", clk / div );//PLL_TO_USE == PLLC ? cpuf / div : coref / div );

  //*(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5);
  //usleep (30);

  //while ( (*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7) );
  //usleep (100);

  /* bits 23:12 integer part of divisor */
  /* bite 11:0 fractional part of divisor */
  /* SOURCE / (DIV_I + DIV_F / 4096) */

  //*(gpclk + (CLK_GP0_DIV / 4)) = CLK_PASSWD | (div_i << 12) | (div_f & 0x7ff);  
  //usleep (30);

  //*(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | PLL_TO_USE | (1 << 4); // 6=plld, 5=pllc
  //usleep (30);

  //while ( ((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0 );
  //usleep (100);

  //SET_GPIO_ALT (PIN_CLK, 0);  // assign clock to gpio pin 4 (PI_CLK)
}


void ps_setup_protocol ( int targetF ) 
{
  //setup_io ();
  create_dev_mem_mapping ();
  setup_gpclk ( targetF );

  *ioclr = TXN_END;

  gpio [0] = GPFSEL0_INPUT;
  gpio [1] = GPFSEL1_INPUT;
  gpio [2] = GPFSEL2_INPUT;

  gpio [0] = GPFSEL0_OUTPUT;
  gpio [1] = GPFSEL1_OUTPUT;
  gpio [2] = GPFSEL2_OUTPUT;

  //printf ( "[INIT] PiSTorm firmware revision 0x%04X\n", ps_fw_rd () );
}


/*
 * Atari ST 8MHz CPU clock
 * clock cycle = 125ns
 * half cycle (pulse) = 63ns
 * 
 * The following reads and writes are optimised to make sure address and data are written
 * to meet bus-cycle requirements. That means address setup has to be done within one cycle (125ns),
 * likewise date too. The Pi4 should easily be capable of doing this
 */

inline
void ps_write_16 ( uint32_t address, uint16_t data )
{
  static uint32_t l;

  //while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );
  //g_buserr = CHECK_BERR (l);
  /*
   * set address first to make sure it's there for S1
   * Pi4 GPIO writes take ~3.5ns, reads take ~100ns
   * Pi3 GPIO writes take ~7.5ns
   * 
   * Pi4 takes ~28ns to write address
   * Pi3 takes ~60ns to write address - pretty tight on a Pi3
   */

   /*
   * data needs to be written ready for S3
   * 
   * Pi4 takes ~14ns to write data
   * Pi3 takes ~30ns to write data
   */
  *ioset = ( data << 8 ) | REG_DATA;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = ( (address & 0xffff) << 8 ) | REG_ADDR_LO;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  *ioclr = TXN_END;

  /* 
   * Atari ST uses a 24 bit address - PiSTorm has 32 bits available 
   * spare 8 bits are used for FC bits and transfer type (read/write)
   * spare bits 7,6,5 FC bits
   * spare bit  1 - write = 0, read = 1
   * spare bit  0 - word = 0, byte = 1
   * 
   * WRITE BYTE = 0x01
   * WRITE WORD = 0x00
   * READ BYTE  = 0x03
   * READ WORD  = 0x02
   */
  /* write 0x00 */
  *ioset = ( ( (fc << 13) | WRITE_WORD | (address >> 16) ) << 8 ) | REG_ADDR_HI;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  *ioclr = TXN_END;

  //*ioset = ( data << 8 ) | REG_DATA;
  //*ioset = PIN_WR;
  //*ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  //*ioclr = TXN_END;

#ifdef IO_DEBUG 
  while ( ( ( l = *ioread ) & PI_TXN_IN_PROGRESS ) && g_timeout );
  g_io_done = 1;
  if ( g_timeout <= 0 )
    printf ( "wr16 timed out 0x%08X, l = 0x%08X\n", address, l );
#else
  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );

#ifdef PI3
  l = *ioread;
#endif
#endif

  //g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);
}


inline
void ps_write_8 ( uint32_t address, uint16_t data ) 
{
  static uint32_t l;

  if ( (address & 1) == 0 )
    data <<= 8;

  else
    data &= 0xff;

  //while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );
  //g_buserr = CHECK_BERR (l);

  *ioset = data << 8 | REG_DATA;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = ((address & 0xffff) << 8) | REG_ADDR_LO;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = (( (fc << 13) | WRITE_BYTE | (address >> 16) ) << 8) | REG_ADDR_HI;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  *ioclr = TXN_END;

  //*ioset = data << 8 | REG_DATA;
  //*ioset = PIN_WR;
  //*ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  //*ioclr = TXN_END;

#ifdef IO_DEBUG 
  while ( ( ( l = *ioread ) & PI_TXN_IN_PROGRESS ) && g_timeout );
  g_io_done = 1;
  if ( g_timeout <= 0 )
    printf ( "wr8 timed out 0x%08X, l = 0x%08X\n", address, l );
#else
  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );

#ifdef PI3
  l = *ioread;
#endif
#endif

  //g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);
}


inline
void ps_write_32 ( uint32_t address, uint32_t value ) 
{
  //ps_write_16 ( address, value >> 16 );
  //ps_write_16 ( address + 2, value );

  static uint32_t l;
  static uint16_t w1, w2;

  w1 = value >> 16;

  *ioset = ( w1 << 8 ) | REG_DATA;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = ( (address & 0xffff) << 8 ) | REG_ADDR_LO;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = ( ( (fc << 13) | 0x0000 | (address >> 16) ) << 8 ) | REG_ADDR_HI;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );

  w2 = value;
  address += 2;

  *ioset = ( w2 << 8 ) | REG_DATA;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = ( (address & 0xffff) << 8 ) | REG_ADDR_LO;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = ( ( (fc << 13) | 0x0000 | (address >> 16) ) << 8 ) | REG_ADDR_HI;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );

  //g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

}


inline
uint16_t ps_read_16 ( uint32_t address ) 
{
	static uint32_t l;
#ifdef PI3
  static uint32_t value;
#endif

  //if ( IO == 0 )
  //  while ( *ioread & PI_TXN_IN_PROGRESS );

  *ioset = ( (address & 0xffff) << 8 ) | REG_ADDR_LO;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = ( ( (fc << 13) | READ_WORD | (address >> 16)  ) << 8 ) |  REG_ADDR_HI;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = REG_DATA;
  *ioset = PIN_RD;
  //*ioclr = PIN_RD; *ioclr = PIN_RD; *ioclr = PIN_RD;

  //IO = 1;

#ifdef IO_DEBUG 
  while ( ( ( l = *ioread ) & PI_TXN_IN_PROGRESS ) && g_timeout );
  g_io_done = 1;
  if ( g_timeout <= 0 )
    printf ( "rd16 timed out 0x%08X, l = 0x%08X\n", address, l );
#else
  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );
#endif

#ifdef PI3
  l = *ioread;
#endif

  //*ioclr = PIN_RD; *ioclr = PIN_RD; *ioclr = PIN_RD;
  //*ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
 	*ioclr = TXN_END;
  
  //g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

return (l >> 8);
}


inline
uint8_t ps_read_8 ( uint32_t address ) 
{
  static uint32_t l;
#ifdef PI3
  static uint32_t value;
#endif

  //if ( IO == 0 )
  //  while ( *ioread & PI_TXN_IN_PROGRESS );

  *ioset = ((address & 0xffff) << 8) | REG_ADDR_LO;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = (( (fc << 13) | READ_BYTE | (address >> 16) ) << 8) | REG_ADDR_HI;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR; //*ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = REG_DATA;
  *ioset = PIN_RD;
  //*ioclr = PIN_RD; *ioclr = PIN_RD; *ioclr = PIN_RD;

  //IO = 1;

#ifdef IO_DEBUG 
  while ( ( ( l = *ioread ) & PI_TXN_IN_PROGRESS ) && g_timeout );
  g_io_done = 1;
  if ( g_timeout <= 0 )
    printf ( "rd8 timed out 0x%08X, l = 0x%08X\n", address, l );
#else
  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );
#endif

#ifdef PI3
  l = *ioread;
#endif

  //*ioclr = PIN_RD; *ioclr = PIN_RD; *ioclr = PIN_RD;
  //*ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;
  
  //g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

if ( (address & 1) == 0 )
  return (l >> 16);

else
  return (l >> 8);
}


uint32_t ps_read_32 ( uint32_t address ) 
{
  //return ( ps_read_16 ( address ) << 16 ) | ps_read_16 ( address + 2 );

  static uint32_t l;
  static uint16_t w1, w2;

  *ioset = ( (address & 0xffff) << 8 ) | REG_ADDR_LO;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = ( ( (fc << 13) | 0x0200 | (address >> 16)  ) << 8 ) |  REG_ADDR_HI;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = REG_DATA;
  *ioset = PIN_RD;

  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );

 	*ioclr = TXN_END;

  address += 2;
  w1 = l >> 8;

  *ioset = ( (address & 0xffff) << 8 ) | REG_ADDR_LO;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;
  
  *ioset = ( ( (fc << 13) | 0x0200 | (address >> 16)  ) << 8 ) |  REG_ADDR_HI;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  *ioset = REG_DATA;
  *ioset = PIN_RD;

  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );

 	*ioclr = TXN_END;
  
  //g_irq = CHECK_IRQ (l);
  g_buserr = CHECK_BERR (l);

  w2 = l >> 8;
  
  return (uint32_t)(w1 << 16) | w2;
}


void ps_write_status_reg ( unsigned int value ) 
{
  *ioset = ((value & 0xffff) << 8) | REG_STATUS;
  *ioset = PIN_WR;
  *ioclr = PIN_WR; *ioclr = PIN_WR; *ioclr = PIN_WR;
  *ioclr = TXN_END;

  while ( *ioread & PI_TXN_IN_PROGRESS );
}

#if (1)
uint16_t ps_read_status_reg () 
{
  static uint32_t l;

  while ( *ioread & PI_TXN_IN_PROGRESS );

  *ioset = REG_STATUS;
  *ioset = PIN_RD;

  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );

#ifdef PI3
  l = *ioread;
#endif

  *ioclr = TXN_END;

  return (l >> 8);
}

#else

uint16_t ps_read_status_reg () 
{
  static uint32_t value;

  while ( *ioread & 1 );

  *ioset = 0x4C; //(REG_STATUS << PIN_A0) | (1 << PIN_RD);

  while ( *ioread & 1 )
    ;

  value = *ioread;

  *ioclr = TXN_END;

#ifdef STATS
  RWstats.rstatus++;
#endif

  return value >> 8;
}
#endif


/* reset state machine */
void ps_reset_state_machine () 
{
  ps_write_status_reg ( STATUS_BIT_INIT );
  usleep ( 10000 );
  ps_write_status_reg ( 0 );
  usleep ( 1000 );
}

/* toggle HALT signal */
void ps_pulse_halt () 
{
  ps_write_status_reg ( STATUS_BIT_HALT );
  usleep ( 250000 );
  ps_write_status_reg ( 0 );
  usleep ( 1000 );
}

/* hold reset low for 250ms - Atari ST needs min 100ms */
void ps_pulse_reset () 
{
  ps_write_status_reg ( STATUS_BIT_RESET );
  usleep ( 250000 );
  ps_write_status_reg ( 0 );
  usleep ( 1000 );
}

/* read CPLD firmware revision */
uint16_t ps_fw_rd ()
{
  uint16_t fw;

  fw = ps_read_status_reg ();
  //printf ( "PiSTorm firmware revision 0x%04X\n", fw );
  return (fw >> 2) & 0x3FF;
}

/* write CPLD firmware revision */
void ps_fw_wr ( uint16_t fwrev )
{
  ps_write_status_reg ( FW_WR | fwrev );
  usleep ( 2500 );
}

/* */
uint16_t ps_read_ipl ()
{
  static uint32_t l;
  static uint8_t a, b;

  while ( ( l = *ioread ) & PI_TXN_IN_PROGRESS );
  //l = *ioread;

  //a = (l & 0x10) >> 2;
  //b = l & 0x02;

  //return ( a | b);
  return ( l );
}