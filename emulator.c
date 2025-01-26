// SPDX-License-Identifier: MIT

#include "platforms/platforms.h"
#include "platforms/atari/IDE.h"
#include "platforms/atari/idedriver.h"
#include "platforms/atari/atari-registers.h"
#include "platforms/atari/pistorm-dev/pistorm-dev-enums.h"
#include "gpio/ps_protocol.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sched.h>
#include <pthread.h>
#include "m68kops.h"
#include <stdbool.h>
#include "platforms/atari/et4000.h"
#include <termios.h>
#include <fcntl.h>

/* test defines */
#define MYWTC 0
#define WATCHDOG 0
//#define IO_DEBUG

/* Blitter defines */
/* mutually exclusive - can only have one */
/* if you want to use real Blitter then comment out both lines */
//#define FAUX_BLITTER
//#define NO_BLITTER

#define DEBUGPRINT 1
#if DEBUGPRINT
#define DEBUG_PRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...) ;
#endif

#define MUSASHI_HAX
#ifdef MUSASHI_HAX
#include "m68kcpu.h"
extern m68ki_cpu_core m68ki_cpu;
extern int m68ki_initial_cycles;
extern volatile int m68ki_remaining_cycles;

#define M68K_END_TIMESLICE 	m68ki_initial_cycles = GET_CYCLES(); \
	SET_CYCLES(0);
#else
#define M68K_SET_IRQ m68k_set_irq
#define M68K_END_TIMESLICE m68k_end_timeslice()
#endif


#define ATARI_MMU_128K  0b00000000 // 0x00 bank 0
#define ATARI_MMU_512K  0b00000100 // 0x04 bank 0
#define ATARI_MMU_1M    0b00000101 // 0x05 bank 0 & 1
#define ATARI_MMU_2M    0b00001000 // 0x08 bank 0 & 1
#define ATARI_MMU_4M    0b00001010 // 0x0A bank 0 & 1

extern void rtgInit ( void );
extern void *et4000Render ( void* );
extern char *get_pistorm_cfg_filename ();
extern void set_pistorm_cfg_filename (char *);
extern uint m68ki_read_imm16_addr_slowpath ( m68ki_cpu_core *state, uint32_t pc );
extern void blitInit ( void );

static inline void m68k_execute_bef ( m68ki_cpu_core *, int );
//void *ide_task ( void* );
//void *misc_task ( void* vptr );
#if WATCHDOG
#include <sys/time.h>

void *wd1_task ();
void *wd2_task ();
volatile int g_wd1, g_wd2;
volatile uint16_t g_status;
#endif

bool FPU68020_SELECTED;
uint8_t emulator_exiting = 0;
volatile int last_irq = 8;
volatile int current_irq = 0;
volatile int cpu_emulation_running = 0;
volatile int passthrough = 0;
//volatile uint32_t do_reset=0;
volatile int pending_irq = 0;
volatile uint16_t g_reset = 0;
volatile int g_int_mask;

uint8_t GPIP;

struct DMA_s {

  uint16_t data_seccount;
  uint16_t status_mode;
  uint8_t  hi;
  uint8_t  mid;
  uint8_t  lo;

};

struct DMA_s DMA;

uint8_t load_new_config = 0;
int mem_fd;
unsigned int cpu_type = M68K_CPU_TYPE_68000;
unsigned int loop_cycles = 20;
struct emulator_config *cfg = NULL;
bool RTG_enabled;
bool RTC_enabled;
bool WTC_enabled;
bool WTC_initialised;
uint32_t ATARI_MEMORY_SIZE;
int RTG_fps;
bool Blitter_enabled;
bool RTG_EMUTOS_VGA;
pthread_mutex_t rtglock; 

//int pirq_first; // debug - used in m68kcpu.h

extern bool IDE_enabled;
extern volatile unsigned int *gpio;
extern uint8_t fc;
extern volatile int g_irq;
extern volatile uint32_t g_buserr;
extern bool RTG_enabled;
extern bool ET4000Initialised;
extern volatile uint32_t RTG_VSYNC;
extern volatile unsigned int *gpio;
extern const char *cpu_types[];
extern volatile int g_timeout;
extern volatile int g_io_done;


void cpu2 ( void )
{
  cpu_set_t cpuset;

	CPU_ZERO ( &cpuset );
	CPU_SET ( 2, &cpuset );
	sched_setaffinity ( 0, sizeof (cpu_set_t), &cpuset );
}


void cpu3 ( void )
{
  cpu_set_t cpuset;

	CPU_ZERO ( &cpuset );
	CPU_SET ( 3, &cpuset );
	sched_setaffinity ( 0, sizeof (cpu_set_t), &cpuset );
}


/* 
 * looking for hangs in instruction loop
 */
#if WATCHDOG
void *wd1_task ()
{
  struct timeval stop, start;
  int took;

  cpu3 ();

  while ( !cpu_emulation_running )
    ;

  while ( cpu_emulation_running )
  {
    if ( g_wd1 )
    {
      gettimeofday ( &start, NULL );

      while ( g_wd1 )
      {
        gettimeofday ( &stop, NULL );
        took = ( (stop.tv_sec - start.tv_sec) * 1000000 ) + (stop.tv_usec - start.tv_usec);

        if ( took > 200000 )
        {
          printf ( "[WATCHDOG] %dus, BR %d, BG %d, AS %d, BGACK %d, PI_TXN %d\n", took, (g_status & 0x400) >> 10, (g_status & 0x200) >> 9, (g_status & 0x100) >> 8, (g_status & 0x080) >> 7, (g_status & 0x040) >> 6 );

          break;
        }

        usleep ( 10 );
      }
    }      
  }
}

void *wd2_task ()
{
  
}
#endif

#if !MYWTC
#define CACHESIZE 4096*1024
#define CACHESIZEBYTES CACHESIZE * sizeof(uint16_t)

uint16_t cache[CACHESIZE]; // top byte used as valid flag
static short flushstate = 0; // 0 active, 1 flush request
pthread_mutex_t cachemutex;

int do_cache( uint32_t address, int size, unsigned int *value, int isread ) {
	// size is 1,2,4
  static short flushstatereq = 0; // go around the houses a bit as don't want to lock for mutex

  if( flushstate > 0) // cache is invalid
    return 0;

  if( flushstatereq && !flushstate ) { // there's been a request to flush and it's not yet set the flush thread's state variable
      if( !pthread_mutex_trylock(&cachemutex) ) {
        flushstate = 1;
        flushstatereq = 0;
        pthread_mutex_unlock(&cachemutex);
      }
      return 0;
  }

  // DMA registers of interest are at 0x00FF8604 (trigger DMA) and 0x00FFFA01 (MFP GPIP bits to check for completion).
  // Sniff reads from 0x00FFFA01 with a mask against 0x20. If this results is 0, a DMA interrupt has triggered
  // Blitter not yet sniffed (disable it)
  if( isread && address == 0x00FFFA01 ) {
    *value = ps_read_8 ( address );
    //if( ( *value & 0x20 ) == 0 ) {
    if( ( (*value & 0x20) || (*value & 0x08) ) == 0 ) {
      flushstatereq=1;
    }
    return 1; // we return success here as we've done the read for you
  }
/*
  if( isread && address == 0x00FFFA09 ) {
    //printf ( "do_cache () got blitter\n" );
    *value = ps_read_8 ( address );
    if( ( *value & 0x08 ) == 1 ) {
      printf ( "do_cache () blitter in use\n" );
      flushstatereq=1;
    }
    return 1; // we return success here as we've done the read for you
  }
*/
  //if( !(address >= 0x000800 && address < 0x400000 ) ) // STRAM only without low RAM (have to perform this check late as sniffing registers above)
  //if( !(address >= 0x0005B0 && (address < (ATARI_MEMORY_SIZE - 0x8000)) ) ) 
  if( !(address >= 0x0005B0 && (address < ATARI_MEMORY_SIZE) ) ) 
    return 0;

	if( isread ) {
    switch(size) {
      case(4):
        if( !(cache[address] & 0x1000) || !(cache[address+1] & 0x01000) || !(cache[address+2] & 0x1000) || !(cache[address+3] & 0x01000) ){
//          printf("MISS\n");
          return 0;
        }
        *value = ((0xff & cache[address]) << 24) | ((0xff & cache[address+1]) << 16) | ((0xff & cache[address+2]) << 8 ) | ((0xff & cache[address+3]));
        return 1;
        break;
      case(2):
        if( !(cache[address] & 0x1000) || !(cache[address+1] & 0x01000) ) {
//          printf("MISS\n");
          return 0;
        }
        *value = ((0xff & cache[address]) << 8) | (0xff & cache[address+1]);
        return 1;
        break;
      case(1):
      default:
        if( (cache[address] & 0x1000) == 0 ) {
//          printf("MISS\n");
          return 0;
        }
        *value = 0xff & cache[address];
//        printf("HIT (%8.8x = %2.2x)\n", address, *value);
        return 1;
        break;
    }
  }
  else {
    switch( size ) {
      case(4):
        cache[address]    = 0x1000 | ( *value >> 24 ); // the 0x10 in top byte indicates valid cache entry
        cache[address+1]  = 0x1000 | ( 0xff & ( *value >> 16 ) );
        cache[address+2]  = 0x1000 | ( 0xff & ( *value >> 8 ) );
        cache[address+3]  = 0x1000 | ( 0xff & *value );
        break;
      case(2):
        cache[address]    = 0x1000 | ( *value >> 8 );
        cache[address+1]  = 0x1000 | ( 0xff & *value );
        break;
      case(1):
      default:
        cache[address]    = 0x1000 | *value;
        break;
    }
  }
  return 1;
}

// separate thread to reduce impact of flushing 8MB memory each time!
void *cacheflusher( void *dummy ) {
  static uint8_t c = 0;

  while( !cpu_emulation_running ) {
    sleep(1);
  }

  while( cpu_emulation_running ) 
  {
    if( flushstate ) 
    {
      memset( cache, 0, CACHESIZEBYTES );
      pthread_mutex_lock( &cachemutex );
      flushstate = 0;
      pthread_mutex_unlock( &cachemutex );
      //printf("Cache flushed [%d]     \r", c++ );
      //fflush(stdout);
    }

    else
      usleep(1e5);
  }
  //printf("End of Flushing thread\n");
}

#else

#define HIT 1
#define MISS 0

static uint8_t *cache_p;
static uint16_t *cacheTable_p;
static short flushstate = 0; // 0 active, 1 flush request
pthread_mutex_t cachemutex;



int do_cache ( uint32_t address, int size, uint32_t *value, int isread ) 
{
  static uint32_t cacheTblAddress;
  static uint16_t cacheHitBits;
  static uint8_t  offset;
  static short    flushstatereq = 0; // go around the houses a bit as don't want to lock for mutex
  static int      ret;


  //if ( flushstate > 0 ) // cache is invalid
  //  return 0;

  if ( flushstatereq ) //&& !flushstate ) // there's been a request to flush and it's not yet set the flush thread's state variable
  {
      //if ( !pthread_mutex_trylock ( &cachemutex ) ) 
      //{
        //flushstate = 1;
        flushstatereq = 0;

      //  pthread_mutex_unlock ( &cachemutex );
      //}
      memset ( cacheTable_p, 0, ATARI_MEMORY_SIZE >> 4 );

      return 0;
  }

  // DMA registers of interest are at 0x00FF8604 (trigger DMA) and 0x00FFFA01 (MFP GPIP bits to check for completion).
  // Sniff reads from 0x00FFFA01 with a mask against 0x20. If this results is 0, a DMA interrupt has triggered
  // Blitter not yet sniffed (disable it)
  if ( isread && address == 0x00FFFA01 ) 
  {
    *value = ps_read_8 ( address );

    if ( ( *value & 0x20 ) == 0 )
    {
      flushstatereq = 1;
    }

    return 1; // we return success here as we've done the read for you
  }    

  // STRAM only without low RAM (have to perform this check late as sniffing registers above)
  if ( ! ( address >= 0x0005B0 && address < ATARI_MEMORY_SIZE  ) )
  //if ( address >= ATARI_MEMORY_SIZE )
    return 0;

	if ( isread ) 
  {
    ret = HIT;
    cacheTblAddress = address >> 4; /* convert address to cache hit table byte */
    offset = address & 0x0f;    /* LSB is used as shift count */

    switch ( size ) 
    {
      /* addresses ending in 0, 4, 8, C */
      case 4:
        cacheHitBits = (0x0F << offset); /* match the bit(s) of the LSB */
        
        if ( ( cacheTable_p [cacheTblAddress] & cacheHitBits ) != cacheHitBits)
        {
         // printf ( "4 MISS address 0x%X, cacheTable 0x%X, cache bits 0x%X\n", address, cacheTblAddress, cacheHitBits );
          ret = MISS;
        }

        else
          *value = (cache_p [address] << 24) | 
              (cache_p [address + 1] << 16) | 
              (cache_p [address + 2] << 8)  | 
                cache_p [address + 3];

        break;

      /* addresses ending in 0, 2, 4, 6, 8, A, C, E */
      case 2:
        cacheHitBits = (0x03 << offset);
        
        if ( ( cacheTable_p [cacheTblAddress] & cacheHitBits ) != cacheHitBits )
        {
         // printf ( "2 MISS address 0x%X, cacheTable 0x%X, cache bits 0x%X - data 0x%X\n", address, cacheTblAddress, cacheHitBits, cacheTable_p [cacheTblAddress] );
          ret = MISS;
        }

        else
          *value = (cache_p [address] << 8) | cache_p [address + 1];
    
        break;

      case 1:
      default:
        cacheHitBits = (0x01 << offset);
     
        if ( ( cacheTable_p [cacheTblAddress] & cacheHitBits ) != cacheHitBits )
        {
        //  printf ( "1 MISS address 0x%X, cacheTable 0x%X, cache bits 0x%X\n", address, cacheTblAddress, cacheHitBits );
          ret = MISS;
        }

        else
          *value = cache_p [address];

        break;
    }

    //printf("HIT (%8.8x = %2.2x)\n", address, *value);
    return ret;
  }

  else 
  {
    cacheTblAddress = address >> 4;
    offset = address & 0x0f;

    switch ( size ) 
    {
      case 4:
        cache_p [address]     = *value >> 24;
        cache_p [address + 1] = *value >> 16;
        cache_p [address + 2] = *value >> 8;
        cache_p [address + 3] = *value;
        cacheTable_p [cacheTblAddress] |= (0x0F << offset);

        break;

      case 2:
        cache_p [address]     = *value >> 8;
        cache_p [address + 1] = *value;
        cacheTable_p [cacheTblAddress] |= (0x03 << offset);
        //printf ( "filling cacheTable 0x%X with 0x%X\n", cacheTblAddress, (0x03 << offset) );
        break;

      case 1:
      default:
        cache_p [address]     = *value;
        cacheTable_p [cacheTblAddress] |= (0x01 << offset);

        break;
    }
  }

  return 1;
}


// separate thread to reduce impact of flushing 8MB memory each time!
// overhead reduced now by using cache bit table - from 8MB down to 512KB
void *cacheflusher ( void *dummy ) 
{
  while ( !cpu_emulation_running ) 
  {
    sleep (1);
  }

  while ( cpu_emulation_running ) 
  {
    if ( flushstate ) 
    {
      memset ( (void *)cacheTable_p, 0, ATARI_MEMORY_SIZE / 16 );

      pthread_mutex_lock ( &cachemutex );
      flushstate = 0;
      pthread_mutex_unlock ( &cachemutex );
      //printf ( "Cache flushed\n" );
    }

    else
      usleep (100); //(1e5);
  }
  //printf("End of Flushing thread\n");
}
#endif




#ifdef IO_DEBUG
void *rw_debug ()
{
  usleep ( 1000000 );  

  while ( !cpu_emulation_running )
    ;

  while ( cpu_emulation_running )
  {
    g_timeout = 100000;
    g_io_done = 0;
    while ( !g_io_done && g_timeout-- );
  }
}
#endif


/*
 * Atari ST Interrupt handling task
 * Offload the detection to this task to optimise the cpu_task ()
 */
void *ipl_task () 
{
  uint16_t g_pending_irq, prev_irq;
  uint16_t status, ipl, berr;
  int32_t timeout;
  //uint8_t kbd;
  //int once;

  usleep ( 1000000 );  

  cpu3 ();

  /* wait for emulation to start */
  while ( !cpu_emulation_running )
    ;

  while ( cpu_emulation_running )
  {
    /* check interrupts when RTG frame buffer is not being drawn */
    if ( ET4000Initialised )
      while ( !RTG_VSYNC )
        ;

    /* read IPL bits */
    status = ps_read_ipl ();
    g_irq = ((status & 0x10) >> 2) | (status & 0x02);

    timeout = 100000;
    //once = 1;

    /* wait for interrupt to be serviced by emulator */
    while ( g_irq && timeout-- )
    {
      //NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
      //NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;
      //NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP; NOP;

      /* read keyboard ACIA */
      //if ( once && !(status & 0x01) )
      //{
      //  kbd = ps_read_8 ( 0x00FFFC00 );
      //  if ( (kbd & 0x70) > 0x0F )
      //    printf ( "ACIA error 0x%X\n", kbd & 0xFF );

      //  once = 0;
      //}
      //else
      usleep (1);
    }
#ifdef DEBUG_INT      
    if ( timeout <= 0 )
    {
      printf ( "IPL timedout - INT MASK is %d - g_irq = %d, 0x%X\n", g_int_mask, g_irq, ps_read_ipl () );
    }
#endif   

    /* 
     * Atari CPU clock is 8MHz = clock cycle of 125ns
     * Atari 68K CPU takes an average of 4 clock cycles per instruction
     * Interrupts should be checked for between instructions
     * Therefore delay for 125ns * 4 = 500ns
     */
    //usleep (1);
  }
}


static uint16_t status;

static inline void m68k_execute_bef ( m68ki_cpu_core *state, int num_cycles )
{
  static uint32_t l;

  /* eat up any reset cycles */
	if (RESET_CYCLES) {
	    int rc = RESET_CYCLES;
	    RESET_CYCLES = 0;
	    num_cycles -= rc;
	    if (num_cycles <= 0)
		return;
	}

	/* Set our pool of clock cycles available */
	SET_CYCLES ( num_cycles );
	m68ki_initial_cycles = num_cycles;

	/* Make sure we're not stopped */
	if ( !CPU_STOPPED )
	{
    g_buserr = 0;

		/* Main loop.  Keep going until we run out of loop cycles */
execute:
    m68ki_use_data_space ();

    REG_PPC = REG_PC;
    //REG_IR = m68ki_read_imm16_addr_slowpath ( state, REG_PC );
    REG_IR = m68ki_read_imm_16 (state);
    m68ki_instruction_jump_table [REG_IR] (state);
    USE_CYCLES ( CYC_INSTRUCTION [REG_IR] );

    if ( g_buserr )
    {
      printf ( "BUS ERROR 0x%08X\n", REG_PC );
      m68ki_exception_bus_error ( state ); 
    }
/*
    else
    {
      m68ki_instruction_jump_table [REG_IR] (state);
      USE_CYCLES ( CYC_INSTRUCTION [REG_IR] );
    }
*/
    // cryptodad make sure m68kcpu.h m68ki_set_sr() has relevent line commented out
    if ( GET_CYCLES () > 0 )
      goto execute;

    REG_PPC = REG_PC;
	}

  else
    SET_CYCLES ( 0 );

	return;
}


struct termios oldt, newt;
int oldf;

void sigint_handler ( int sig_num ) 
{
  cpu_emulation_running = 0;
  
  DEBUG_PRINTF ( "\n[MAIN] Exiting\n" );
  
  if ( mem_fd )
    close ( mem_fd );

  if ( cfg->platform->shutdown ) 
  {
    cfg->platform->shutdown ( cfg );
  }

  while ( !emulator_exiting ) 
  {
    emulator_exiting = 1;
    usleep ( 0 );
  }

  /* reset stdio tty properties */
  oldt.c_lflag |= ECHO;
  tcsetattr ( STDIN_FILENO, TCSANOW, &oldt );
  fcntl ( STDIN_FILENO, F_SETFL, oldf );
#if MYWTC
  free ( cacheTable_p );
  free ( cache_p );
#endif

  if ( WTC_initialised )
    pthread_mutex_destroy ( &cachemutex );

  exit ( 0 );
}



void *cpu_task() 
{
  //const struct sched_param priority = {99};
  uint16_t status;
	m68ki_cpu_core *state = &m68ki_cpu;
  int statem, prev_state, fcount;
  uint16_t reset, ipl;
  int lc;
  
  state->gpio = gpio;
	m68k_pulse_reset(state);

  usleep ( 1000000 );  

  cpu2 (); // anchor task to cpu2 
  //sched_setscheduler ( 0, SCHED_FIFO, &priority );
  //system ( "echo -1 >/proc/sys/kernel/sched_rt_runtime_us" );

  //mlockall ( MCL_CURRENT );  // lock in memory to keep us from paging out

  while ( !cpu_emulation_running )
    ;

run:
#if (0)
  status = ps_read_status_reg ();
  
  if ( status == 0xFFFF )
    printf ( "bad status\n" );

  else if ( status & 0x2 ) 
  {
    M68K_END_TIMESLICE;

    DEBUG_PRINTF ( "[CPU] Emulation reset\n");

    usleep ( 1000000 ); 

    m68k_pulse_reset ( state );
  }

  else
  {
    current_irq = status >> 13;

    if ( current_irq != last_irq )
    {
      last_irq = current_irq;

      m68k_set_irq ( current_irq );
      m68ki_check_interrupts ( state );
    }
  }

#else

  lc = loop_cycles;
  ipl = g_irq;

  if ( g_reset ) 
  {
    M68K_END_TIMESLICE;

    DEBUG_PRINTF ( "[CPU] Emulation reset\n");

    usleep ( 100000 ); 

    m68k_pulse_reset ( state );

    g_reset = 0;
    g_irq = 0;
    g_buserr = 0;
  }

  else if ( g_buserr )
  {
    if ( ipl )
    {
      m68k_set_irq ( 0 );
      g_irq = 0;
    }

   // g_buserr = 0;

    //m68k_execute ( state, loop_cycles );
  }

  else if ( ipl )//&& !g_buserr )
  {
    //ipl = g_irq;
    
    //if ( ipl )
    //{
      m68k_set_irq ( ipl );
      m68ki_check_interrupts ( state );
      g_int_mask = FLAG_INT_MASK >> 8;

      //if ( ipl <= g_int_mask )
      //  last_irq = ipl;

      //else if ( last_irq > ipl )
      //  last_irq = 0;

      g_irq = 0;
    //}
    

    //m68k_execute ( state, 1 );
    lc = 1;
  }

  //else
  //{
  //  g_buserr = 0;
  //  m68k_execute ( state, loop_cycles );
  //}

  m68k_execute_bef ( state, lc );

#endif  

  if ( !cpu_emulation_running )
  {
    printf ("[CPU] End of CPU thread\n");

    return (void *)NULL;
  }

  goto run; /* cryptodad - goto is faster than using while () */
}


int main ( int argc, char *argv[] ) 
{
  const struct sched_param priority = {99};
  int g;
  int err;
  pthread_t rtg_tid, cpu_tid, flush_tid, ipl_tid, rw_debug_tid;
#if WATCHDOG
  pthread_t wd1_tid, wd2_tid;
  int errwd1, errwd2;
#endif
  time_t t;
  int targetF = 200;

  RTG_EMUTOS_VGA = false;
  RTG_enabled = false;
  FPU68020_SELECTED = false;
  RTC_enabled = false;
  WTC_enabled = false;
  WTC_initialised = false;
  Blitter_enabled = false;
  RTG_fps = 0;
  

  // Some command line switch stuffles
  for ( g = 1; g < argc; g++ ) 
  {
    if ( strcmp ( argv[g], "--config-file") == 0 || strcmp(argv[g], "--config" ) == 0 ) 
    {
      if (g + 1 >= argc) 
      {
        DEBUG_PRINTF ( "%s switch found, but no config filename specified.\n", argv[g] );
      } 
      
      else 
      {
        g++;
        FILE *chk = fopen ( argv[g], "rb" );

        if ( chk == NULL ) 
        {
          DEBUG_PRINTF ( "Config file %s does not exist, please check that you've specified the path correctly.\n", argv[g] );
        } 
        
        else 
        {
          fclose ( chk );
          load_new_config = 1;
          set_pistorm_cfg_filename ( argv[g] );
        }
      }
    }

    if ( strcmp ( argv [g], "--clock" ) == 0 )
    {
      if ( g + 1 >= argc ) 
      {
        DEBUG_PRINTF ( "%s switch found, but missing parameter.\n", argv[g] );
      } 

      else
      {
        g++;
        targetF = atoi ( argv [g] );
      }
    }
  }

  if ( load_new_config ) 
  {
    uint8_t config_action = load_new_config - 1;
    load_new_config = 0;
    if (cfg) {
      free_config_file(cfg);
      free(cfg);
      cfg = NULL;
    }

    switch(config_action) 
    {
      case PICFG_LOAD:
      case PICFG_RELOAD:
        cfg = load_config_file ( get_pistorm_cfg_filename () );
        break;
      //case PICFG_DEFAULT:
      //  cfg = load_config_file("default.cfg");
      //  break;
    }
  }

  if (!cfg) 
  {
    DEBUG_PRINTF ("No config file specified\n");
    
    return 1;
  }

  if (cfg) 
  {
    if (cfg->cpu_type) 
      cpu_type = cfg->cpu_type;

    if (cfg->loop_cycles) 
      loop_cycles = cfg->loop_cycles;
    
    else if ( loop_cycles == 0 )
      loop_cycles = 12;

    if ( !cfg->platform )
    {
      cfg->platform = make_platform_config ( "atari", "st" );
      printf ( "[CFG] Plaform not specified - using Atari ST\n" );
    }

    cfg->platform->platform_initial_setup ( cfg );
    // debug stuff
    //printf ( "mapped_low = %p, mapped_high = %p\n", cfg->mapped_low, cfg->mapped_high );
  }

  signal ( SIGINT, sigint_handler );

  /* save stdio tty properties and ammend for emulator use */
  /* tty properties are restored in sigint_handler () */
  
  tcgetattr ( STDIN_FILENO, &oldt );
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr ( STDIN_FILENO, TCSANOW, &newt );
  oldf = fcntl ( STDIN_FILENO, F_GETFL, 0 );
  fcntl ( STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK );

  //mlockall ( MCL_CURRENT );  // lock in memory to keep us from paging out

  ps_setup_protocol ( targetF );
  ps_reset_state_machine ();
  ps_pulse_reset ();
  ps_pulse_halt ();

#if WATCHDOG == 0
  /* 
   * WIP - read CPLD firmware version 
   * 11 bit version data 
   */
  uint16_t fw = ps_fw_rd ();

  /* 
   * if alpha release, then all 8 bits will be version number 
   * eg. 0.1a - 0.255a 
   */
  if ( (fw & 0x600) == 0x200 )
    //printf ( "[INIT] PiSTorm firmware %s 0.%da\n",
    //  (fw & 0x100) ? "EPM570" : "EPM240",
    //  (fw & 0xff) );
    printf ( "[INIT] PiSTorm firmware 0.%da\n",
      (fw & 0xff) );

  /* firmware version is beta or release */
  else
    printf ( "[INIT] PiSTorm firmware %s %d.%d%c\n",
      (fw & 0x100) ? "EPM570" : "EPM240",
      (fw & 0xe0) >> 5,
      (fw & 0x1f),
      (fw & 0x600) == 0x400 ? 'b' : 'r' );
#endif

  usleep ( 1500 );
  
  m68k_init ();
	m68k_set_cpu_type ( &m68ki_cpu, cpu_type );
  m68k_set_int_ack_callback ( &cpu_irq_ack );

  cpu_pulse_reset ();
  ps_pulse_halt ();


  /* Initialise Interfaces */
  InitIDE ();

  if ( RTG_enabled )
  {
    rtgInit ();
    et4000Init ();

    printf ( "[RTG] ET4000 Initialised\n" );
  }

  if ( Blitter_enabled )
  {
    blitInit ();

    printf ( "[MAIN] Faux Blitter Initialised\n" );
  }
  
  err = pthread_create ( &cpu_tid, NULL, &cpu_task, NULL );

#if WATCHDOG
  errwd1 = pthread_create ( &wd1_tid, NULL, &wd1_task, NULL );
  errwd2 = pthread_create ( &wd2_tid, NULL, &wd2_task, NULL );
#endif

  if ( err != 0 )
    DEBUG_PRINTF ( "[ERROR] Cannot create CPU thread: [%s]", strerror ( err ) );

  else 
  {
    pthread_setname_np ( cpu_tid, "pistorm: cpu" );
    printf ( "[MAIN] CPU thread created successfully\n" );
  }

#ifdef IO_DEBUG
  err = pthread_create ( &rw_debug_tid, NULL, &rw_debug, NULL );

  if ( err != 0 )
    DEBUG_PRINTF ( "[ERROR] Cannot create RW DEBUG thread: [%s]", strerror ( err ) );

  else 
  {
    pthread_setname_np ( rw_debug_tid, "pistorm: rwdebug" );
    printf ( "[MAIN] RW DEBUG thread created successfully\n" );
  }
#endif


  err = pthread_create ( &ipl_tid, NULL, &ipl_task, NULL );

  if ( err != 0 )
    DEBUG_PRINTF ( "[ERROR] Cannot create IPL thread: [%s]", strerror ( err ) );

  else 
  {
    pthread_setname_np ( ipl_tid, "pistorm: ipl" );
    printf ( "[MAIN] IPL thread created successfully\n" );
  }





  if ( ET4000Initialised )
  {
    //if ( pthread_mutex_init ( &rtglock, NULL) != 0 ) 
    //{ 
    //    DEBUG_PRINTF ( "\n mutex init has failed\n" );  
    //} 

    err = pthread_create ( &rtg_tid, NULL, &et4000Render, NULL );

    if ( err != 0 )
      DEBUG_PRINTF ( "[ERROR] Cannot create RTG thread: [%s]", strerror (err) );

    else 
    {
      pthread_setname_np ( rtg_tid, "pistorm: rtg" );
      printf ( "[MAIN] RTG thread created successfully\n" );
    }
  }
  
  /* get Atari memory size */
  /* NOTE this ONLY works if stable communication between Pi and Atari i.e. ataritest works */
  /* NOTE jan 2025 - bus error is NOT generated on addresses falling within the first 4MB */
  uint32_t s;

  printf ( "[MAIN] Checking physical ATARI memory... " );

  cpu_set_fc ( 6 );
  ATARI_MEMORY_SIZE = 0;
  g_buserr = 0;

  /* configure MMU for all 4MB *not sure if this is correct for smaller memory sizes */
  ps_write_8 ( (uint32_t)0x00ff8001, ATARI_MMU_4M ); 
  
  /* check permutations - 512KB, 1MB, 2MB, 4MB */
  for ( s = 0x00080000; s < 0x00800000; s <<= 1 )
  {     
    ps_write_8 ( s, 0x55 );

    usleep ( 1 );

    /* now check we can read back same data */
    if ( ps_read_8 ( s ) != 0x55 ) 
      break;

    usleep ( 1 );
  }
  
  g_buserr = 0;
  cpu_set_fc ( 6 );
  ATARI_MEMORY_SIZE = s;

  if ( ATARI_MEMORY_SIZE )
    printf ( "found %d KB of RAM installed\n", ATARI_MEMORY_SIZE / 1024 );

  else
  {
    printf ( "None found - Cannot proceed\n" );

    sigint_handler (9);
  }


  if ( WTC_enabled )
  {
#if MYWTC
    cache_p = malloc ( ATARI_MEMORY_SIZE );
    cacheTable_p = malloc ( ATARI_MEMORY_SIZE / 16 );

    if ( cache_p && cacheTable_p )
    {
#endif
      WTC_initialised = true;
#if (1)
      pthread_mutex_init ( &cachemutex, NULL );
      err = pthread_create ( &flush_tid, NULL, &cacheflusher, NULL );

      if ( err != 0 )
        DEBUG_PRINTF ( "[ERROR] Cannot create WTC Flushing thread: [%s]", strerror (err) );

      else 
      {
        pthread_setname_np ( rtg_tid, "pistorm: flusher" );
        printf ( "[MAIN] WTC Flushing thread created successfully\n" );
      }
#endif

#if MYWTC
    }

    else
    {
      DEBUG_PRINTF ( "[ERROR] Failed to allocate memory to WTC\n", strerror (err) );
    }
#endif
  }


  /* Start Emulation */
  cpu_emulation_running = 1; /* start the threads running - up until now, they are just waiting/looping  */

  time ( &t ); /* get date and time */

  printf ( "[MAIN] Emulation Running [%s%s] %s\n", 
      cpu_types [cpu_type - 1], 
      (cpu_type == M68K_CPU_TYPE_68020 && FPU68020_SELECTED) ? " + FPU" : "",
      ctime ( &t ) );

  /* cryptodad optimisation - .cfg no mappings */
  if ( cfg->mapped_high == 0 && cfg-> mapped_low == 0 )
    passthrough = 1;
  
  else
    passthrough = 0;

  if ( passthrough )
    printf ( "[MAIN] %s Native Performance\n", cpu_types [cpu_type - 1] );

  printf ( "[MAIN] Press CTRL-C to terminate\n" );
  printf ( "\n" );

  //sched_setscheduler ( 0, SCHED_FIFO, &priority );
  //system ( "echo -1 >/proc/sys/kernel/sched_rt_runtime_us" );
#if WATCHDOG == 0
  //cpu3 (); // anchor main task to cpu3 
#endif

  //if ( ET4000Initialised )
 //   pthread_join ( rtg_tid, NULL );

  //else
    pthread_join ( cpu_tid, NULL );

  printf ("[MAIN] Emulation Ended\n");

  return 0;
}


/* 
 * CPU RESET instruction has been called 
 * NOTE this instruction toggles the hardware RESET signal. It DOES NOT reset the CPU,
 * nor should it touch the HALT signal
 */
void cpu_pulse_reset ( void ) 
{
  printf ("%s CPU RST\n", __func__ );
  ps_pulse_reset (); // toggle hardware reset signal

  if ( WTC_initialised )
  {
    pthread_mutex_lock( &cachemutex );
    flushstate = 1;
    pthread_mutex_unlock( &cachemutex );
  }
#if (0)
  /* clear ATARI system vectors and system variables */
  for ( uint32_t n = 0x380; n < 0x5B4; n += 2 )
    ps_write_16 ( n, 0 );
#endif

  /* re-initialise graphics */
  if ( ET4000Initialised )
    et4000Init ();

}



static uint32_t mapped_data = 0;
static uint32_t platform_res, rres;

#if (0)
/* return 24 bit address */
static inline uint32_t check_ff_st ( uint32_t add ) 
{
	if ( ( add & 0xFF000000 ) == 0xFF000000 ) 
    add &= 0x00FFFFFF;

	return add;
}
#endif

/* levels 2 and 4 are video syncs, so thousands are coming in */
inline uint16_t cpu_irq_ack ( int level ) 
{
  static uint32_t ack;
  static uint8_t vec;

  // CPU interrupt acknowledge FC bits set
  cpu_set_fc ( 0x7 );
  ack = 0x00fffff0 | (level << 1);
  vec = ps_read_16 ( ack );
  
  if ( level < 6 ) // autovectors
  //if ( level == 2 || level == 4 ) // autovectors
  	return 24 + level;
  
  return vec;
}




extern int blitRead ( uint8_t type, uint32_t addr, uint32_t *res );
extern int blitWrite ( uint8_t type, uint32_t addr, uint32_t val );

static inline uint32_t check_ff_st ( uint32_t add ) 
{
	if ( ( add & 0xFF000000 ) == 0xFF000000 ) 
  {
		add &= 0x00FFFFFF;
	}

	return add;
}

static inline int32_t platform_read_check ( uint8_t type, uint32_t addr, uint32_t *res ) 
{ 
  static uint32_t v;

#if defined FAUX_BLITTER
  /* Faux Blitter */
  if ( Blitter_enabled && (addr >= 0x00FF8A00 && addr < 0x00FF8A3E) )
  {
    addr &= 0x00FFFFFF;

    blitRead ( type, addr, res );
    //printf ( "blitter read 0x%X, data = 0x%X\n", addr, *res );

    return 1;
  }
#else 
 #if defined NO_BLITTER
  /* EMUtos disable 68000 Blitter */
  if ( (addr >= 0x00FF8A00 && addr < 0x00FF8A3E) )
  {
    g_buserr = 1;
    *res = 0xFF;

    return 1;
  }
 #endif
#endif
  g_buserr = 0;

  if ( ET4000Initialised && ( (addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_REGTOP) ))//|| (addr >= 0xFEC00000 && addr < 0xFEDC0400) ) )
  {
    while ( !RTG_VSYNC )
      ;

    v = et4000Read ( addr, res, type );

    g_buserr = 0;

    return v;
  }

  /* GPIP check */
  //if ( addr == 0x00FFFA01 )
  //{
    //printf ( "reading GPIP 0x%X\n", GPIP );
  //  *res = GPIP;

  //  return 1;
  //}

  /* 
   * Set Atari's date/time - read by TOS program -> PISTORM.PRG
   * Needed two spare words to write the BCD data/time data to
   * FFFC40 is undefined in the Atari-Compendium, so used that
   * PISTORM.PRG reads these two 16 bit addresses, then writes the date/time to IKBD
   * 
   * This will only be used once when Atari boots 
   */
  if ( RTC_enabled && (addr >= 0x00FFFC40 && addr < 0x00FFFC44) )
  {
    uint16_t atari_dt;
    uint16_t atari_tm;
    time_t t = time ( NULL );
    struct tm tm = *localtime ( &t );

    atari_dt  = (tm.tm_year - 80) << 9;
    atari_dt |= (tm.tm_mon + 1) << 5;
    atari_dt |=  tm.tm_mday;

    atari_tm  = tm.tm_hour << 11;
    atari_tm |= tm.tm_min << 5;
    atari_tm |= tm.tm_sec / 2;

    if ( addr == 0x00FFFC40 )
      *res = atari_dt;

    else if ( addr == 0x00FFFC42 )
      *res = atari_tm;

    return 1;
  }

  if ( IDE_enabled && (addr >= IDEBASEADDR && addr < IDETOPADDR) )
  {
    addr &= 0x00ffffff;
  }

  if ( ( addr >= cfg->mapped_low && addr < cfg->mapped_high ) )
  {
    if ( handle_mapped_read ( cfg, addr, &mapped_data, type ) != -1 ) 
    {
      if ( g_buserr )
        printf ( "mapped read got a berr 0x%X\n", addr );

      g_buserr = 0; /* can't have a buss error if mapped memory is used */

      *res = mapped_data;
      
      return 1;
    }
  }

  return 0;
}


unsigned int m68k_read_memory_8 ( uint32_t address ) 
{
  static uint32_t value;
  static uint32_t r;

  //if ( ET4000Initialised )
  //  pthread_mutex_lock ( &rtglock ); 

  if ( platform_read_check ( OP_TYPE_BYTE, address, &platform_res ) ) 
  {
   // if ( ET4000Initialised )
    //  pthread_mutex_unlock ( &rtglock ); 

    return platform_res;
  }

  address = check_ff_st( address );
  if ( address & 0xFF000000 )
    return 0;
/*
  if ( ( address & 0xFF000000 ) == 0xFF000000 )
    address &= 0x00FFFFFF;

  // this check is only needed for EMUtos determining amount of ALT-RAM - looks like only the write is needed 

  if ( address & 0xFF000000 )
  {
    printf ( "8rd why here? address = 0x%X\n", address );
 
    return 0;
  }
*/
/*
  if ( address == 0xFFFC21 )
  {
    printf ( "Looking for Mega ST RTC\n" );
    g_buserr = 1;
    return 0;
  }
  

  if ( address >= 0xA00000 && address < 0xDF0000 )
  {
    printf ( "8rd Looking for Mega STe VME bus\n" );
    g_buserr = 1;
    return 0;
  }
*/

  if ( WTC_initialised )
  {
    if ( do_cache ( address, 1, &value, 1 ) )
      return value;
  }

  r = ps_read_8 ( address );  

  //if ( ET4000Initialised )
  //  pthread_mutex_unlock ( &rtglock ); 
#ifdef INTERCEPT_DMA
  /* GPIP check */
  if ( address == 0x00FFFA01 )
  {
    //printf ( "reading GPIP 0x%X\n", GPIP );
    GPIP = r;
  }

  /* DMA */
  else if ( address >= 0x00FF8609 && address < 0x00FF860E )
  {
    printf ( "DMA read8 0x%X, 0x%X\n", address, value & 0xFF );

    if ( address == 0x00FF8609 )
      DMA.hi = r;

    else if ( address == 0x00FF860B )
      DMA.mid = r;

    else if ( address == 0x00FF860D )
      DMA.lo = r;
  }
 #endif 
  return r;
}


unsigned int m68k_read_memory_16 ( uint32_t address ) 
{
  static uint32_t value;
  static uint32_t r;

  //if ( ET4000Initialised )
  //  pthread_mutex_lock ( &rtglock ); 

  if ( platform_read_check ( OP_TYPE_WORD, address, &platform_res ) ) 
  {
    //if ( ET4000Initialised )
    //  pthread_mutex_unlock ( &rtglock ); 

    return platform_res;
  }

  address = check_ff_st( address );
  if ( address & 0xFF000000 )
    return 0;
/*
  if ( ( address & 0xFF000000 ) == 0xFF000000 ) 
    address &= 0x00FFFFFF;

  if ( address & 0xFF000000 )
  {
    printf ( "16rd why here? address = 0x%X\n", address );
    
    return 0;
  }
*/
/*
  if ( address >= 0xA00000 && address < 0xDF0000 )
  {
    printf ( "16rd Looking for Mega STe VME bus\n" );
    g_buserr = 1;
    return 0;
  }
*/
  if ( WTC_initialised )
  {
    if ( do_cache ( address, 2, &value, 1 ) )
      return value;
  }
  
  r = ps_read_16 ( address );

  //if ( ET4000Initialised )
  //  pthread_mutex_unlock ( &rtglock ); 
#ifdef INTERCEPT_DMA
  /* DMA */
  if ( address >= 0x00FF8604 && address < 0x00FF8608 )
  {
    printf ( "DMA read16 0x%X, 0x%X\n", address, value & 0xFFFF );

    if ( address == 0x00FF8604 )
      DMA.data_seccount = r;

    else if ( address == 0x00FF8606 )
      DMA.status_mode = r;
  }
#endif
  return r;
}


unsigned int m68k_read_memory_32 ( uint32_t address ) 
{
  static uint32_t value;
  static uint32_t r;

  //if ( ET4000Initialised )
  //  pthread_mutex_lock ( &rtglock ); 

  if ( platform_read_check ( OP_TYPE_LONGWORD, address, &platform_res ) ) 
  {
    //if ( ET4000Initialised )
    //  pthread_mutex_unlock ( &rtglock ); 

    return platform_res;
  }

  address = check_ff_st( address );
  if ( address & 0xFF000000 )
    return 0;
/*    
  if ( ( address & 0xFF000000 ) == 0xFF000000 ) 
    address &= 0x00FFFFFF;

  if ( address & 0xFF000000 )
  {
    printf ( "32rd why here? address = 0x%X\n", address );
    return 0;
  }
*/
/*
  if ( address >= 0xA00000 && address < 0xDF0000 )
  {
    printf ( "32rd Looking for Mega STe VME bus\n" );
    //g_buserr = 1;
    return 0;
  }
*/
  if ( WTC_initialised )
  {
    if ( do_cache( address, 4, &value, 1 ) )
      return value;
  }

  r = ps_read_32 ( address );

  //if ( ET4000Initialised )
  //  pthread_mutex_unlock ( &rtglock ); 

  return r;
}


static inline int32_t platform_write_check ( uint8_t type, uint32_t addr, uint32_t val ) 
{
  static int r;

#if defined FAUX_BLITTER
  /* Faux Blitter */
  if ( Blitter_enabled && (addr >= 0x00FF8A00 && addr < 0x00FF8A3E) )
  {
    //printf ( "blitter write 0x%X, data = 0x%X\n", addr, val );

    addr &= 0x00FFFFFF;

    blitWrite ( type, addr, val );

    return 1;
  }
#else
 #if defined NO_BLITTER
  /* EMUtos disable 68000 Blitter */
  if ( (addr >= 0x00FF8A00 && addr < 0x00FF8A3E) )
  {
    g_buserr = 1;

    return 1;
  }
 #endif
#endif
  g_buserr = 0;

  if ( ET4000Initialised && ( (addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_REGTOP) ))// || (addr >= 0xFEC00000 && addr < 0xFEDC0400) ) )
  {
    //printf ( "calling et4000Write () with addr 0x%X\n", addr );
    //pthread_mutex_lock ( &rtglock ); 
    while ( !RTG_VSYNC )
      ;

    et4000Write ( addr, val, type );
    
    //pthread_mutex_unlock ( &rtglock ); 
    g_buserr = 0;

    return 1;
  }

  /* GPIP check */
  //if ( addr == 0x00FFFA01 )
  //{
  //  printf ( "writing GPIP 0x%X\n", val & 0xFF );
  //  GPIP = val;

  //  return 1;
  //}

  if ( IDE_enabled && (addr >= IDEBASEADDR && addr < IDETOPADDR) )
  {
    addr &= 0x00ffffff;
  }

  if ( ( addr >= cfg->mapped_low && addr < cfg->mapped_high ) ) 
  {
    if ( handle_mapped_write ( cfg, addr, val, type ) != -1 )
    {
      if ( g_buserr )
        printf ( "mapped write got a berr 0x%X\n", addr );

      g_buserr = 0; /* can't have a buss error if mapped memory is used */
      
      return 1;
    }
  }

  return 0;
}


void m68k_write_memory_8 ( uint32_t address, unsigned int value ) 
{
  //if ( ET4000Initialised )
  //  pthread_mutex_lock ( &rtglock ); 

  if ( platform_write_check ( OP_TYPE_BYTE, address, value ) )
  {
    //if ( ET4000Initialised )
    //  pthread_mutex_unlock ( &rtglock ); 

    return;
  }

  address = check_ff_st( address );
  if ( address & 0xFF000000 )
    return;
/*
  if ( ( address & 0xFF000000 ) == 0xFF000000 ) 
    address &= 0x00FFFFFF;

  //  this check is only needed for EMUtos determining amount of ALT-RAM 
  
  if ( address & 0xFF000000 )
  {
    printf ( "8wr why here? address = 0x%X\n", address );
    //if ( ET4000Initialised )
    //  pthread_mutex_unlock ( &rtglock ); 

    return;
  }
*/
  //if ( ET4000Initialised )
  //  pthread_mutex_lock ( &rtglock ); 
#ifdef INTERCEPT_DMA
  /* GPIP check */
  if ( address == 0x00FFFA01 )
    ps_write_8 ( address, GPIP );
  
  /* DMA */
  else if ( address >= 0x00FF8609 && address < 0x00FF860E )
  {
    printf ( "DMA write8 0x%X, 0x%X\n", address, value & 0xFF );

    //if ( address == 0x00FF8609 )
    //  ps_write_8 ( address, DMA.hi );

   // else if ( address == 0x00FF860B )
    //  ps_write_8 ( address, DMA.mid );

    //else if ( address == 0x00FF860D )
    //  ps_write_8 ( address, DMA.lo );
  }
#endif
  //else
    ps_write_8 ( address, value );

  //if ( ET4000Initialised )
  //  pthread_mutex_unlock ( &rtglock ); 

  if ( WTC_initialised )
    do_cache ( address, 1, &value, 0 );
}


void m68k_write_memory_16 ( uint32_t address, unsigned int value ) 
{
  //if ( ET4000Initialised )
  //  pthread_mutex_lock ( &rtglock ); 

  if ( platform_write_check ( OP_TYPE_WORD, address, value ) )
  {
    //if ( ET4000Initialised )
    //  pthread_mutex_unlock ( &rtglock ); 

    return;
  }

  address = check_ff_st( address );
  if ( address & 0xFF000000 )
    return;
#ifdef INTERCEPT_DMA
  /* DMA */
  if ( address >= 0x00FF8604 && address < 0x00FF8608 )
  {
    printf ( "DMA write16 0x%X, 0x%X\n", address, value & 0xFFFF );

   // if ( address == 0x00FF8604 )
    //  ps_write_16 ( address, DMA.data_seccount );

   // else if ( address == 0x00FF8606 )
    //  ps_write_16 ( address, DMA.status_mode );
  }
#endif
  //else
    ps_write_16 ( address, value );
  
  //if ( ET4000Initialised )
  //  pthread_mutex_unlock ( &rtglock ); 

  if ( WTC_initialised )
    do_cache ( address, 2, &value, 0 );
}


void m68k_write_memory_32 ( uint32_t address, unsigned int value ) 
{
  //if ( ET4000Initialised )
  //  pthread_mutex_lock ( &rtglock ); 

  if ( platform_write_check ( OP_TYPE_LONGWORD, address, value ) )
  {
    //if ( ET4000Initialised )
    //  pthread_mutex_unlock ( &rtglock ); 

    return;
  }

  address = check_ff_st( address );
  if ( address & 0xFF000000 )
    return;

  //if ( ET4000Initialised )
  //  pthread_mutex_lock ( &rtglock ); 
    
  ps_write_32 ( address, value );
  
  //if ( ET4000Initialised )
  //  pthread_mutex_unlock ( &rtglock ); 

  if ( WTC_initialised )
    do_cache ( address, 4, &value, 0 );
}


void cpu_set_fc ( unsigned int _fc ) 
{
	fc = _fc;
}
