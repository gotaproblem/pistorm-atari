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
#define ATARI_MMU_2M    0b00001000 // 0x08 bank 0
#define ATARI_MMU_4M    0b00001010 // 0x0A bank 0 & 1

extern void rtgInit ( void );
extern void *et4000Render ( void* );
extern char *get_pistorm_cfg_filename ();
extern void set_pistorm_cfg_filename (char *);
extern uint m68ki_read_imm16_addr_slowpath ( m68ki_cpu_core *state, uint32_t pc );
extern void blitInit ( void );
extern uint8_t ps_read_8 ( uint32_t address );


static inline void m68k_execute_bef ( m68ki_cpu_core *, int );
//void *ide_task ( void* );
//void *misc_task ( void* vptr );

bool FPU68020_SELECTED;
uint8_t emulator_exiting = 0;
volatile uint32_t last_irq = 8;
//volatile uint32_t last_last_irq = 8;
volatile int cpu_emulation_running = 0;
volatile int passthrough = 0;
//volatile uint32_t do_reset=0;

uint8_t load_new_config = 0;
int mem_fd;
unsigned int cpu_type = M68K_CPU_TYPE_68000;
unsigned int loop_cycles = 12;
struct emulator_config *cfg = NULL;
bool RTG_enabled;
bool RTC_enabled;
bool WTC_enabled;
bool WTC_initialised;
uint32_t ATARI_MEMORY_SIZE;
int RTG_fps;
bool Blitter_enabled;
bool RTG_EMUTOS_VGA;
//volatile uint16_t g_status;

extern bool IDE_enabled;
extern volatile unsigned int *gpio;
extern uint8_t fc;
extern volatile uint16_t g_irq;
extern volatile uint32_t g_buserr;
extern bool RTG_enabled;
extern bool ET4000Initialised;
extern volatile unsigned int *gpio;
extern const char *cpu_types[];
extern volatile bool PS_LOCK;
//extern volatile bool ioDone;
//extern volatile bool g_iack;


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
    if( ( *value & 0x20 ) == 0 ) {
    //if( ( (*value & 0x20) || (*value & 0x08) ) == 0 ) {
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


  if ( flushstate > 0 ) // cache is invalid
    return 0;

  if ( flushstatereq && !flushstate ) // there's been a request to flush and it's not yet set the flush thread's state variable
  {
      if ( !pthread_mutex_trylock ( &cachemutex ) ) 
      {
        flushstate = 1;
        flushstatereq = 0;

        pthread_mutex_unlock ( &cachemutex );
      }
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
  if ( ! ( address >= 0x0005B0 && (address < ATARI_MEMORY_SIZE) ) )
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
          //printf ( "4 MISS address 0x%X, cacheTable 0x%X, cache bits 0x%X\n", address, cacheTblAddress, cacheHitBits );
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
          //printf ( "2 MISS address 0x%X, cacheTable 0x%X, cache bits 0x%X\n", address, cacheTblAddress, cacheHitBits );
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
          //printf ( "1 MISS address 0x%X, cacheTable 0x%X, cache bits 0x%X\n", address, cacheTblAddress, cacheHitBits );
          ret = MISS;
        }

        else
          *value = cache_p [address];

        break;
    }

    //if ( ret == HIT )
    //  printf ( "HIT (%8.8x = %2.2x)\n", address, *value );

    return ret;
  }

  else 
  {
    cacheTblAddress = address >> 4;
    offset = address & 0x0f;
    //printf ( "cache write 0x%X 0x%X size = %d, offset = 0x%X - cacheTblAddress = 0x%X\n", address, *value, size, offset, cacheTblAddress );
    
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
      memset ( cacheTable_p, 0, ATARI_MEMORY_SIZE >> 4 );
      memset ( cache_p, 0, ATARI_MEMORY_SIZE );

      pthread_mutex_lock ( &cachemutex );
      flushstate = 0;
      pthread_mutex_unlock ( &cachemutex );
      //printf ( "Cache flushed\n" );
    }

    else
      usleep (1000); //(1e5);
  }
  //printf("End of Flushing thread\n");
}
#endif


#if (1)
static inline void m68k_execute_bef ( m68ki_cpu_core *state, int num_cycles )
{
	/* Set our pool of clock cycles available */
	SET_CYCLES ( num_cycles );
	m68ki_initial_cycles = num_cycles;

	/* Make sure we're not stopped */
	if ( !CPU_STOPPED )
	{
		/* Main loop.  Keep going until we run out of clock cycles */
execute:      
    m68ki_use_data_space ();

    REG_PPC = REG_PC;
    REG_IR = m68ki_read_imm16_addr_slowpath ( state, REG_PC );
      
    m68ki_instruction_jump_table [REG_IR] (state);

    USE_CYCLES ( CYC_INSTRUCTION [REG_IR] );

    if ( g_buserr )
    {
      m68ki_exception_bus_error ( state ); 
      g_buserr = 0;
    }

    //else
    //  USE_CYCLES ( CYC_INSTRUCTION [REG_IR] );

    // cryptodad make sure m68kcpu.h m68ki_set_sr() has relevent line commented out
    if ( GET_CYCLES () > 0 )
      goto execute;

    REG_PPC = REG_PC;
	}

  else
    SET_CYCLES ( 0 );

	return;
}
#else
/* Execute some instructions until we use up num_cycles clock cycles */
/* ASG: removed per-instruction interrupt checks */
static inline void m68k_execute_bef ( m68ki_cpu_core *state, int num_cycles )
{
	/* eat up any reset cycles */
	if (RESET_CYCLES) {
	    int rc = RESET_CYCLES;
	    RESET_CYCLES = 0;
	    num_cycles -= rc;
	    //if (num_cycles <= 0)
		//return rc;
	}

	/* Set our pool of clock cycles available */
	SET_CYCLES(num_cycles);
	m68ki_initial_cycles = num_cycles;

	/* See if interrupts came in */
	//m68ki_check_interrupts(state);

	/* Make sure we're not stopped */
	if(!CPU_STOPPED)
	{
		/* Return point if we had an address error */
		m68ki_set_address_error_trap(state); /* auto-disable (see m68kcpu.h) */

		//m68ki_check_bus_error_trap();


		/* Main loop.  Keep going until we run out of clock cycles */
		do
		{
			/* Set tracing according to T1. (T0 is done inside instruction) */
			m68ki_trace_t1(); /* auto-disable (see m68kcpu.h) */

			/* Set the address space for reads */
			m68ki_use_data_space(); /* auto-disable (see m68kcpu.h) */

			/* Call external hook to peek at CPU */
			m68ki_instr_hook(REG_PC); /* auto-disable (see m68kcpu.h) */

			/* Record previous program counter */
			REG_PPC = REG_PC;

			/* Read an instruction and call its handler */
			REG_IR = m68ki_read_imm_16(state);
			m68ki_instruction_jump_table[REG_IR](state);
			USE_CYCLES(CYC_INSTRUCTION[REG_IR]);

      if ( g_buserr )
      {
        m68ki_exception_bus_error ( state ); 
      }

			/* Trace m68k_exception, if necessary */
			m68ki_exception_if_trace(state); /* auto-disable (see m68kcpu.h) */
		} while(GET_CYCLES() > 0);

		/* set previous PC to current PC for the next entry into the loop */
		REG_PPC = REG_PC;
	}
	else
		SET_CYCLES(0);

	/* return how many clocks we used */
	//return m68ki_initial_cycles - GET_CYCLES();
}
#endif
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


void *cpu_task () 
{
  const struct sched_param priority = {99};
  uint16_t status;
	m68ki_cpu_core *state = &m68ki_cpu;

  state->gpio = gpio;
	m68k_pulse_reset(state);

  usleep ( 1000000 );  

  //cpu3 (); // anchor task to cpu3 
  //sched_setscheduler ( 0, SCHED_FIFO, &priority );
  //system ( "echo -1 >/proc/sys/kernel/sched_rt_runtime_us" );

  while ( !cpu_emulation_running )
    ;

run:
  m68k_execute_bef ( state, loop_cycles );

#if (0)
  status = ps_read_status_reg ();
  
  //if ( status != 0xFFFF )
  //  printf ( "IPL %d\n", status >> 13 );

  if ( status == 0xFFFF )
    printf ( "bad status\n" );

  else if ( status & 0x2 ) 
  {
    M68K_END_TIMESLICE;

    DEBUG_PRINTF ( "[CPU] Emulation reset - status 0x%X\n", status );

    usleep ( 1000000 ); 

    m68k_pulse_reset ( state );
  }

  //else
  {
    last_irq = status >> 13;

    if ( last_irq )
    {
      m68k_set_irq ( last_irq );
      m68ki_check_interrupts ( state );
    }
  }

#else

#if (0)
  if ( g_irq )
  {
    status = ps_read_status_reg ();

    if ( status == 0xFFFF )
    {
      printf ( "bad status\n" );
    }

    else
    {
      if ( status & 0x2 ) 
      {
        M68K_END_TIMESLICE;

        printf ( "[CPU] Emulation reset - status = 0x%X\n", status );

        usleep ( 500000 ); 

        m68k_pulse_reset ( state );
      }

      else
      {
        last_irq = status >> 13;

        //printf ( "IPL %d\n", last_irq );

        if ( last_irq != 0 )
        {
          m68k_set_irq ( last_irq );
          m68ki_check_interrupts ( state );
        }
      }
    }
  }

  else
  {
    m68k_set_irq ( 0 );
    m68ki_check_interrupts ( state );
  }  
#else

  status = ps_read_status_reg ();

  if ( status == 0xFFFF )
  {
    printf ( "bad status\n" );
  }

  else
  {
    if ( status & 0x2 ) 
    {
      M68K_END_TIMESLICE;

      printf ( "[CPU] Emulation reset - status = 0x%X\n", status );

      usleep ( 1000000 ); 

      m68k_pulse_reset ( state );
    }

    else
    {
      last_irq = status >> 13;

      //printf ( "IPL %d\n", last_irq );

      if ( last_irq != 0 )
      {
        m68k_set_irq ( last_irq );
        m68ki_check_interrupts ( state );
      }
    }
  }

  if ( last_irq == 0 )
  {
    m68k_set_irq ( 0 );
    m68ki_check_interrupts ( state );
  }  
  
#endif  
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
  pthread_t rtg_tid, cpu_tid, flush_tid;
  time_t t;
#ifndef PI3
  int targetF = 125;
#else
  int targetF = 200;
#endif
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

    if (!cfg->platform)
    {
      cfg->platform = make_platform_config ( "atari", "st" );
      printf ( "[CFG] Plaform not specified - using Atari ST\n" );
    }

    cfg->platform->platform_initial_setup ( cfg );
  }

  /* determine ROM */
  bool ROM = 0;
  

  signal ( SIGINT, sigint_handler );

  /* save stdio tty properties and ammend for emulator use */
  /* tty properties are restored in sigint_handler () */
  
  tcgetattr ( STDIN_FILENO, &oldt );
  newt = oldt;
  newt.c_lflag &= ~(ICANON | ECHO);
  tcsetattr ( STDIN_FILENO, TCSANOW, &newt );
  oldf = fcntl ( STDIN_FILENO, F_GETFL, 0 );
  fcntl ( STDIN_FILENO, F_SETFL, oldf | O_NONBLOCK );

  mlockall ( MCL_CURRENT );  // lock in memory to keep us from paging out

  ps_setup_protocol ( targetF );
  ps_reset_state_machine ();
  ps_pulse_reset ();

  usleep (1500);
 
  m68k_init ();
	m68k_set_cpu_type ( &m68ki_cpu, cpu_type );
  m68k_set_int_ack_callback ( &cpu_irq_ack );

  cpu_pulse_reset ();

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

  if ( err != 0 )
    DEBUG_PRINTF ( "[ERROR] Cannot create CPU thread: [%s]", strerror ( err ) );

  else 
  {
    pthread_setname_np ( cpu_tid, "pistorm: cpu" );
    printf ( "[MAIN] CPU thread created successfully\n" );
  }

  if ( ET4000Initialised )
  {
    err = pthread_create ( &rtg_tid, NULL, &et4000Render, NULL );

    if ( err != 0 )
      DEBUG_PRINTF ( "[ERROR] Cannot create RTG thread: [%s]", strerror (err) );

    else 
    {
      pthread_setname_np ( rtg_tid, "pistorm: rtg" );
      printf ( "[MAIN] RTG thread created successfully\n" );
    }
  }
  
  /* 
   * determine Atari memory size 
   */
  printf ( "[MAIN] Checking physical ATARI memory... " );

  fc = 6;
  ATARI_MEMORY_SIZE = 0;

  ps_write_8 ( ((uint32_t)0x00ff8001), ATARI_MMU_4M ); // configure MMU for max amount of memory

  usleep ( 5 );

  for ( int m = 0, s = 0x00080000; m < 4; m++, s <<= 1 )
  {     
    usleep ( 5 );

    ps_write_8 ( s, 0x12 );
    ps_write_8 ( s + 1, 0x56 );

    usleep ( 5 );

    if ( ps_read_8 ( s ) != 0x12 || ps_read_8 ( s + 1 ) != 0x56 )
    {
      ATARI_MEMORY_SIZE = s;

      break;
    }
  }
  
  if ( ATARI_MEMORY_SIZE )
    printf ( "found %d KB of RAM installed\n", ATARI_MEMORY_SIZE / 1024 );

  else
  {
    printf ( "None found - Cannot proceed\n" );
    //printf ("[MAIN] Emulation Ended\n");

    sigint_handler (9);
  }

  fc = 6;
  g_buserr = 0;

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
  //cpu3 (); // anchor main task to cpu3 

  if ( ET4000Initialised )
    pthread_join ( rtg_tid, NULL );

  else
    pthread_join ( cpu_tid, NULL );

  printf ("[MAIN] Emulation Ended\n");

  return 0;
}


/* CPU RESET instruction has been called */
void cpu_pulse_reset ( void ) 
{
  if ( WTC_initialised )
  {
    pthread_mutex_lock( &cachemutex );
    flushstate = 1;
    pthread_mutex_unlock( &cachemutex );
  }

  /* re-initialise graphics */
  if ( ET4000Initialised )
    et4000Init ();

  //printf ( "reset instruction\n" );
  ps_pulse_reset ();

  /* clear ATARI system vectors and system variables */
  //for ( uint32_t n = 0x380; n < 0x5B4; n += 2 )
  for ( uint32_t n = 0x8; n < 0x5B4; n += 2 )
    ps_write_16 ( n, 0 );
}



static uint32_t target = 0;
static uint32_t platform_res, rres;


/* levels 2 and 4 are video syncs, so thousands are coming in */
inline uint16_t cpu_irq_ack ( int level ) 
{
  static uint32_t ack;
  static uint8_t vec;

  fc  = 0x7; // CPU interrupt acknowledge
  ack = 0x00fffff0 | (level << 1);
  vec = ps_read_16 ( ack );
  
  //if ( level < 6 ) // autovectors
  if ( level == 2 || level == 4 ) // autovectors
  	return 24 + level;
  
  return vec;
}




extern int blitRead ( uint8_t type, uint32_t addr, uint32_t *res );
extern int blitWrite ( uint8_t type, uint32_t addr, uint32_t val );

static inline int32_t platform_read_check ( uint8_t type, uint32_t addr, uint32_t *res ) 
{
  static int r;

/* Faux Blitter */
#if (1)
  if ( Blitter_enabled && (addr >= 0x00FF8A00 && addr < 0x00FF8A3E) )//|| (addr >= 0xFFFF8A00 && addr < 0xFFFF8A3E) )
  {
    addr &= 0x00FFFFFF;

    blitRead ( type, addr, res );
    //printf ( "blitter read 0x%X, data = 0x%X\n", addr, *res );

    return 1;
  }

/* No Blitter */
#else
  /* EMUtos disable 68000 Blitter */
  if ( (addr >= 0x00FF8A00 && addr < 0x00FF8A3E) )// || (addr >= 0xFFFF8A00 && addr < 0xFFFF8A3E) )
  {
    g_buserr = 1;
    *res = 0xFF;

    return 1;
  }
#endif

  if ( ET4000Initialised && ( (addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_REGTOP) || (addr >= 0xFEC00000 && addr < 0xFEDC0400) ) )
  //if ( ET4000Initialised && (addr >= 0x00A00000 && addr < 0x00DFFFFF) )
  {
    //printf ( "calling et4000Read () with addr 0x%X\n", addr );
   
    r = et4000Read ( addr, res, type );

    return r;
  }

  /* Set Atari's date/time - picked up by TOS program -> pistorm.prg */
  /* FFFC40 is undefined in the Atari-Compendium, coming after MSTe RTC defines */
  /* pistorm.prg reads these two 16bit addresses, then writes date/time to IKBD */
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
    if ( handle_mapped_read ( cfg, addr, &target, type ) != -1 ) 
    {
      *res = target;
      
      return 1;
    }
  }

  return 0;
}

unsigned int m68k_read_memory_8 ( uint32_t address ) 
{
  static uint32_t value;
  static uint32_t r;

  PS_LOCK = true;

  if ( platform_read_check ( OP_TYPE_BYTE, address, &platform_res ) ) 
  {
    PS_LOCK = false;

    return platform_res;
  }

  if ( ( address & 0xFF000000 ) == 0xFF000000 )
    address &= 0x00FFFFFF;

  /* this check is only needed for EMUtos determining amount of ALT-RAM - looks like only the write is needed */
  /*
  if ( address & 0xFF000000 )
  {
    printf ( "8rd why here? address = 0x%X\n", address );
    return 0;
  }
  */

  if ( WTC_initialised )
  {
    if ( do_cache ( address, 1, &value, 1 ) )
    {
      PS_LOCK = false;

      return value;
    }
  }

/*
  if ( address == 0xFFFA03 )
  {
    uint8_t x = ps_read_8 ( 0xFFFA03 );
    if ( !(x & 0x20) )
    {
      //printf ( "IDE interrupt 0x%X\n", x );
      //ps_write_8 ( 0xFFFA03, x & 0xDF );
      ps_write_8 ( 0xFFFA03, x | 0x20 );
    }
  }
*/

  r = ps_read_8 ( address ); 

  PS_LOCK = false;
  
  return r;
}


unsigned int m68k_read_memory_16 ( uint32_t address ) 
{
  static uint32_t value;
  static uint32_t r;

  PS_LOCK = true;

  if ( platform_read_check ( OP_TYPE_WORD, address, &platform_res ) ) 
  {
    PS_LOCK = false;

    return platform_res;
  }

  if ( ( address & 0xFF000000 ) == 0xFF000000 ) 
    address &= 0x00FFFFFF;

  /*
  if ( address & 0xFF000000 )
  {
    printf ( "16rd why here? address = 0x%X\n", address );
    return 0;
  }
  */

  if ( WTC_initialised )
  {
    if ( do_cache ( address, 2, &value, 1 ) )
    {
      PS_LOCK = false;

      return value;
    }
  }

  r = ps_read_16 ( address );

  PS_LOCK = false;
  
  return r;
}


unsigned int m68k_read_memory_32 ( uint32_t address ) 
{
  static uint32_t value;
  static uint32_t r;

  PS_LOCK = true;

  if (platform_read_check ( OP_TYPE_LONGWORD, address, &platform_res ) ) 
  {
    PS_LOCK = false;

    return platform_res;
  }

  if ( ( address & 0xFF000000 ) == 0xFF000000 ) 
    address &= 0x00FFFFFF;

  /*
  if ( address & 0xFF000000 )
  {
    printf ( "32rd why here? address = 0x%X\n", address );
    return 0;
  }
  */

  if ( WTC_initialised )
  {
    if ( do_cache( address, 4, &value, 1 ) )
    {
      PS_LOCK = false;

      return value;
    }
  }

  r = ps_read_32 ( address );

  PS_LOCK = false;
  
  return r;
}


static inline int32_t platform_write_check ( uint8_t type, uint32_t addr, uint32_t val ) 
{
  static int r;

/* Faux Blitter */
#if (1)
  if ( Blitter_enabled && (addr >= 0x00FF8A00 && addr < 0x00FF8A3E) )//|| (addr >= 0xFFFF8A00 && addr < 0xFFFF8A3E) )
  {
    //printf ( "blitter write 0x%X, data = 0x%X\n", addr, val );

    addr &= 0x00FFFFFF;

    blitWrite ( type, addr, val );

    return 1;
  }

/* No Blitter */
#else
  /* EMUtos disable 68000 Blitter */
  if ( (addr >= 0x00FF8A00 && addr < 0x00FF8A3E) )// || (addr >= 0xFFFF8A00 && addr < 0xFFFF8A3E) )
  {
    g_buserr = 1;

    return 1;
  }
#endif

  if ( ET4000Initialised && ( (addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_REGTOP) || (addr >= 0xFEC00000 && addr < 0xFEDC0400) ) )
  //if ( ET4000Initialised && (addr >= 0x00A00000 && addr < 0x00DFFFFF) )
  {
    //RTG_LOCK = true;
    //printf ( "calling et4000Write () with addr 0x%X\n", addr );
    et4000Write ( addr, val, type );

    //RTG_LOCK = false;

    return 1;
  }

  if ( IDE_enabled && (addr >= IDEBASEADDR && addr < IDETOPADDR) )
  {
    addr &= 0x00ffffff;
  }

  if ( ( addr >= cfg->mapped_low && addr < cfg->mapped_high ) ) 
  {
    if ( handle_mapped_write ( cfg, addr, val, type ) != -1 )
    {
      return 1;
    }
  }

  return 0;
}


void m68k_write_memory_8 ( uint32_t address, unsigned int value ) 
{
  PS_LOCK = true;

  if ( platform_write_check ( OP_TYPE_BYTE, address, value ) )
  {
    PS_LOCK = false;
    return;
  }
   
  if ( ( address & 0xFF000000 ) == 0xFF000000 ) 
    address &= 0x00FFFFFF;

  /* this check is only needed for EMUtos determining amount of ALT-RAM */
  if ( address & 0xFF000000 )
  {
    //printf ( "8wr why here? address = 0x%X\n", address );
    PS_LOCK = false;
    return;
  }

  ps_write_8 ( address, value );

  if ( WTC_initialised )
    do_cache ( address, 1, &value, 0 );

  PS_LOCK = false;
}


void m68k_write_memory_16 ( uint32_t address, unsigned int value ) 
{
  PS_LOCK = true;

  if ( platform_write_check ( OP_TYPE_WORD, address, value ) )
  {
    PS_LOCK = false;
    return;
  }

  if ( ( address & 0xFF000000 ) == 0xFF000000 ) 
    address &= 0x00FFFFFF;

  /*
  if ( address & 0xFF000000 )
  {
    printf ( "16wr why here? address = 0x%X\n", address );
    return;
  }
  */

  ps_write_16 ( address, value );

  if ( WTC_initialised )
    do_cache ( address, 2, &value, 0 );

  PS_LOCK = false;
}


void m68k_write_memory_32 ( uint32_t address, unsigned int value ) 
{
  PS_LOCK = true;

  if ( platform_write_check ( OP_TYPE_LONGWORD, address, value ) )
  {
    PS_LOCK = false;
    return;
  }

  if ( ( address & 0xFF000000 ) == 0xFF000000 ) 
    address &= 0x00FFFFFF;

  /*
  if ( address & 0xFF000000 )
  {
    printf ( "32wr why here? address = 0x%X\n", address );
    return;
  }
  */
 
  ps_write_32 ( address, value );

  if ( WTC_initialised )
    do_cache ( address, 4, &value, 0 );

  PS_LOCK = false;
}


void cpu_set_fc ( unsigned int _fc ) 
{
	fc = _fc;
}
