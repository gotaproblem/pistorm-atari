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

#define CHECK_BERR 1
#define WAIT_IO_START 0
//

volatile unsigned int *gpio;
volatile unsigned int *gpclk;

unsigned int gpfsel0;
unsigned int gpfsel1;
unsigned int gpfsel2;

unsigned int gpfsel0_o;
unsigned int gpfsel1_o;
unsigned int gpfsel2_o;

//int g_irq;

//#if CHECK_BERR
void (*callback_berr)(uint16_t status, uint32_t address, int mode) = NULL;
//#endif

uint8_t fc;
volatile uint32_t readLock;

#define CHECK_PIN_RESET(x) (!(x & 0x20))
#define CHECK_PIN_IN_PROGRESS(x) ((x & 0x01))

//#if CHECK_BERR
void set_berr_callback( void(*ptr)(uint16_t,uint32_t,int) ) {
	callback_berr = ptr;
}
//#endif

static void setup_io() {
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

#define PLLC_200MHZ 6 /* ARM clock / 6 eg. 1200 MHz / 6 = 200 MHz*/
#define PLLC_100MHZ 7 /* ARM clock / 7 = 100 MHz */
#define PLLC_50MHZ 8 /* ARM clock / 8 = 50 MHz */
#define PLLC_25MHZ 9 /* ARM clock / 8 = 25 MHz */

#define PLLD_200MHZ 2 /* CORE clock / 2 eg. 400 MHz / 2 = 200 MHz */
#define PLLD_100MHZ 3 /* CORE clock / 3 = 100 MHz */
#define PLLD_50MHZ 4 /* CORE clock / 4 = 50 MHz */
#define PLLD_25MHZ 5 /* CORE clock / 5 = 25 MHz */

/* chabge these 2 settings to configure clock */
#define PLL PLLD
#define PLL_DIVISOR PLLD_200MHZ

static void setup_gpclk() {
  // Enable 200MHz CLK output on GPIO4, adjust divider and pll source depending
  // on pi model
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | (1 << 5);
  usleep(10);

  while ((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7));
  usleep(100);

//#if USE_PLLC
  *(gpclk + (CLK_GP0_DIV / 4)) = CLK_PASSWD | (PLL_DIVISOR << 12);  // divider , 6=200MHz on pi3
  usleep(10);
  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | PLL | (1 << 4);  // pll? 6=plld, 5=pllc
//#elif USE_PLLD
//  *(gpclk + (CLK_GP0_DIV / 4)) = CLK_PASSWD | (PLLD_DIV3 << 12); /* gpu clock (plld) divisor  2 = 200 MHz, 3 = 100 MHz */
//  usleep(10);
//  *(gpclk + (CLK_GP0_CTL / 4)) = CLK_PASSWD | PLLD | (1 << 4);
//#endif
  
  usleep(10);

  while (((*(gpclk + (CLK_GP0_CTL / 4))) & (1 << 7)) == 0)    ;
  usleep(100);

  SET_GPIO_ALT(PIN_CLK, 0);  // assign clock to this gpio pin (PI_CLK)
}

void ps_setup_protocol() {
  setup_io();
  setup_gpclk();

  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}

#if CHECK_BERR
void check_berr(uint32_t address, int mode) {
  
  uint16_t status = ps_read_status_reg();
	//g_irq = status >> 13;
  //printf("%s: status: %x, irq = %d, address = %08x, mode = %d\n", __func__, status, g_irq, address, mode ); 
  if( (status & STATUS_BIT_BERR) && callback_berr ) {
      //printf("status: %x\n", status );
      //printf("%s: status: %x, irq = %d, address = %08x, mode = %d\n", __func__, status, g_irq, address, mode ); 
      callback_berr(status,address,mode);  
  }
}
#endif


void ps_write_16(unsigned int address, unsigned int data) 
{
  static unsigned int l;
#if WAIT_IO_START
  while ( *(gpio + 13) & 1) ; /* do not attempt txn until HW is ready */
#endif
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;


  *(gpio + 7) = (( address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;


  *(gpio + 7) = (( (fc<<13) | 0x0000 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;


  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

	while ( ( l = *(gpio + 13) ) & 1 );
 // {
#if CHECK_BERR
	  if ( CHECK_PIN_RESET(l) )
    {
		  check_berr ( address, 0 );
     // break;
    }
#endif
  //}

}


void ps_write_8(unsigned int address, unsigned int data) 
{
  static unsigned int l;
#if WAIT_IO_START
  while ( *(gpio + 13) & 1) ; /* do not attempt txn until HW is ready */
#endif
  if ((address & 1) == 0)
    data = data + (data << 8);  // EVEN, A0=0,UDS
  else
    data = data & 0xff;  // ODD , A0=1,LDS

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((data & 0xffff) << 8) | (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = (( (fc<<13) | 0x0100 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

	while ( ( l = *(gpio + 13) ) & 1 );
 // {
#if CHECK_BERR
	  if ( CHECK_PIN_RESET(l) )
    {
		  check_berr ( address, 0 );
     // break;
    }
#endif
  //}
}

void ps_write_32(unsigned int address, unsigned int value) {
  ps_write_16(address, value >> 16);
  ps_write_16(address + 2, value);
}

#define NOP asm("nop"); asm("nop");

#if (0)
unsigned int ps_read_16(unsigned int address) 
{
	static unsigned int l;
  unsigned int value;

  //while ( (*(gpio + 13) & 1) == 1 ) ; /* do not attempt txn until HW is ready */

  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 7) = ( ( (fc<<13) | 0x0200 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);
  *(gpio + 7) = 1 << PIN_RD;

  while ( (l = *(gpio + 13) ) & 0x1 ) // 0x1 == PIN_TXN_IN_PROGRESS
    ; 

  value = *(gpio + 13);
 	*(gpio + 10) = 0xffffec;

  if( CHECK_PIN_RESET(l) )
    check_berr(address,1);

//usleep(1);
  return (value >> 8) & 0xffff;
}

#else

//long int nanoMax = 0;
//long int nanoMin = 1000000;

uint32_t ps_read_16 ( uint32_t address ) 
{
  static uint32_t l;
  uint32_t value;
#if WAIT_IO_START
  while ( *(gpio + 13) & 1) ; /* do not attempt txn until HW is ready */
#endif
  long int nanoStart;
  long int nanoEnd;
  long int nanoDiff;
  long int nanoAvg;
  struct timespec tmsStart, tmsEnd;
  //clock_gettime ( CLOCK_REALTIME, &tmsStart );

  GPFSEL_OUTPUT;

  GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFF));
  GPIO_WRITEREG(REG_ADDR_HI, ( (fc << 13) | 0x0200 | (address >> 16) ) );

  GPFSEL_INPUT;
  GPIO_PIN_RD;

  //WAIT_TXN;
  while ( ( l = *(gpio + 13) ) & 0x1 ) ; 
  
  value = ((*(gpio + 13) >> 8) & 0xFFFF);

  //clock_gettime ( CLOCK_REALTIME, &tmsEnd );

  END_TXN;

#if CHECK_BERR
  if ( CHECK_PIN_RESET(l) )
    check_berr ( address, 1 );
#endif

  //nanoStart = (tmsStart.tv_sec) + (tmsStart.tv_nsec);
  //nanoEnd   = (tmsEnd.tv_sec) + (tmsEnd.tv_nsec);
  //nanoDiff  = nanoEnd - nanoStart;
  //if ( nanoDiff > nanoMax )
  //  nanoMax = nanoDiff;

  //if ( nanoDiff < nanoMin )
  //  nanoMin = nanoDiff;

  //printf ( "ps_read_16 min %ld, max %ld, current %ld nanoseconds\n", nanoMin, nanoMax, nanoDiff );

  return value;
}
#endif


unsigned int ps_read_8(unsigned int address) 
{
  static unsigned int l;
#if WAIT_IO_START
  while ( *(gpio + 13) & 1) ; /* do not attempt txn until HW is ready */
#endif
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  
  *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec; /* clear GPIOs 2,3,5,6,7 */

  *(gpio + 7) = (( (fc<<13) | 0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);// | (1 << PIN_WR);
  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;

  *(gpio + 7) = (REG_DATA << PIN_A0);// | (1 << PIN_RD);
  *(gpio + 7) = 1 << PIN_RD;

	unsigned int value;
  while ( ( l = *(gpio + 13) ) & 1 ) {} // 0x1 == PIN_TXN_IN_PROGRESS

  value = *(gpio + 13);
  *(gpio + 10) = 0xffffec;

	value = (value >> 8) & 0xffff;

#if CHECK_BERR
  if ( CHECK_PIN_RESET(l) )
		check_berr ( address, 1 );
#endif

  if ((address & 1) == 0)
    return (value >> 8) & 0xff;  // EVEN, A0=0,UDS
  else
    return value & 0xff;  // ODD , A0=1,LDS
}


unsigned int ps_read_32(unsigned int address) {
  return (ps_read_16(address) << 16) | ps_read_16(address + 2);
}

void ps_write_status_reg(unsigned int value) 
{
#if WAIT_IO_START
  while ( *(gpio + 13) & 1) ; /* do not attempt txn until HW is ready */
#endif
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((value & 0xffff) << 8) | (REG_STATUS << PIN_A0);

  *(gpio + 7) = 1 << PIN_WR;
  *(gpio + 7) = 1 << PIN_WR; // delay
#ifdef CHIP_FASTPATH
  *(gpio + 7) = 1 << PIN_WR; // delay 210810
#endif
  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}


 

unsigned int ps_read_status_reg() 
{
//#if WAIT_IO_START
 // while ( *(gpio + 13) & 1) ; /* do not attempt txn until HW is ready */
//#endif
  //readLock = 1;
  *(gpio + 7) = (REG_STATUS << PIN_A0);// | (1 << PIN_RD);
  *(gpio + 7) = 1 << PIN_RD;
  *(gpio + 7) = 1 << PIN_RD;
  //*(gpio + 7) = 1 << PIN_RD;
  //*(gpio + 7) = 1 << PIN_RD;
#ifdef CHIP_FASTPATH
  *(gpio + 7) = 1 << PIN_RD; // delay 210810
  *(gpio + 7) = 1 << PIN_RD; // delay 210810
#endif

  unsigned int value;// = *(gpio + 13);
  //uint32_t timeout = 1000000;
  while ( *(gpio + 13) & 1 ) ;//(1 << PIN_TXN_IN_PROGRESS ) )
  //{
   // if ( timeout-- )
   //   continue;

   // value = 0xffffffff;
   // break;
  //}
  value = *(gpio + 13);
  *(gpio + 10) = 0xffffec;

  //readLock = 0;
  return (value >> 8) & 0xffff;
}


void ps_reset_state_machine() {
  ps_write_status_reg(STATUS_BIT_INIT);
  usleep(1500);
  ps_write_status_reg(0);
  usleep(100);
}


void ps_pulse_reset() {
  ps_write_status_reg(0);
  usleep(100000);
  ps_write_status_reg(STATUS_BIT_RESET);
}

unsigned int ps_get_ipl_zero() {
  unsigned int value = *(gpio + 13);
  while ((value=*(gpio + 13)) & (1 << PIN_TXN_IN_PROGRESS)) {}
  return value & (1 << PIN_IPL_ZERO);
}

//#define INT2_ENABLED 1
#if (0)
void ps_update_irq() {
  unsigned int ipl = 0;

  if (!ps_get_ipl_zero()) {
    //while (readLock);
    unsigned int status = ps_read_status_reg();
    ipl = (status & STATUS_MASK_IPL) >> 13;
    printf ( "%s: status = 0x%04X, ipl = %d\n", __func__, status, ipl );
  }

  /*if (ipl < 2 && INT2_ENABLED && emu_int2_req()) {
    ipl = 2;
  }*/

  m68k_set_irq(ipl);
}
#endif


void
ps_config ()
{
  *(gpio + 0) = GPFSEL0_OUTPUT;
  *(gpio + 1) = GPFSEL1_OUTPUT;
  *(gpio + 2) = GPFSEL2_OUTPUT;

  *(gpio + 7) = ((PS_CNF_CPU & 0xffff) << 8) | (REG_STATUS << PIN_A0);

  *(gpio + 7) = (1 << PIN_WR) | (1 << PIN_RD);

  *(gpio + 10) = 1 << PIN_WR;
  *(gpio + 10) = 0xffffec;

  *(gpio + 0) = GPFSEL0_INPUT;
  *(gpio + 1) = GPFSEL1_INPUT;
  *(gpio + 2) = GPFSEL2_INPUT;
}