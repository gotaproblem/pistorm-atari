//#define DISASSEMBLE
// SPDX-License-Identifier: MIT

#include "platforms/platforms.h"
#include "platforms/atari/IDE.h"
#include "platforms/atari/idedriver.h"
#include "platforms/atari/pistorm-dev/pistorm-dev-enums.h"
#include "gpio/ps_protocol.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sched.h>
#ifdef RTG
#include <pthread.h>
#endif
#include "m68kops.h"



#define DEBUGPRINT 1
#if DEBUGPRINT
#define DEBUG_PRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...) ;
#endif

void *ide_task ( void* );
void *misc_task ( void* vptr );

extern char *get_pistorm_cfg_filename();
extern void set_pistorm_cfg_filename (char *);

extern uint8_t IDE_IDE_enabled;
extern volatile unsigned int *gpio;
extern uint8_t fc;

uint8_t emulator_exiting = 0;
volatile uint32_t old_level;

volatile uint32_t last_irq = 8;
volatile uint32_t last_last_irq = 8;

extern volatile int g_irq;
extern volatile int g_buserr;

uint8_t load_new_config = 0;

int mem_fd;
int mem_fd_gpclk;
volatile int irq;
volatile int cpu_emulation_running = 0;
volatile int passthrough = 0;

#ifdef RTG
extern void rtg ( int, uint32_t, uint32_t );
extern volatile uint32_t RTG_ATARI_SCREEN_RAM;
extern int RTG_enabled;
#endif

FILE *console = NULL;

#define MUSASHI_HAX

#ifdef MUSASHI_HAX
#include "m68kcpu.h"
extern m68ki_cpu_core m68ki_cpu;
extern int m68ki_initial_cycles;
extern int m68ki_remaining_cycles;

#define M68K_END_TIMESLICE 	m68ki_initial_cycles = GET_CYCLES(); \
	SET_CYCLES(0);
#else
#define M68K_SET_IRQ m68k_set_irq
#define M68K_END_TIMESLICE m68k_end_timeslice()
#endif

// Configurable emulator options
unsigned int cpu_type = M68K_CPU_TYPE_68000;
unsigned int loop_cycles = 20, irq_status = 0;
struct emulator_config *cfg = NULL;

volatile uint32_t do_reset=0;
volatile uint32_t gotIntLevel;
volatile uint32_t ipl;

//void call_berr(uint16_t status, uint32_t address, uint mode);
static inline void m68k_execute_bef(m68ki_cpu_core *, int);

volatile uint32_t g_vector;

extern volatile unsigned int *gpio;
uint16_t irq_delay = 0;
#define TIMEOUT 10000000ul; // arbitary value, so long as it's big enough

#define ORIGINAL
#define ATARI_VID 0
#if ATARI_VID
int ATARI_VID_enabled = 1; // cryptodad Apr 2023
#else
int ATARI_VID_enabled = 0; 
#endif

#if (0)
void *ipl_task ( void *args ) 
{
  uint16_t old_irq = 0;
  uint32_t value;
  int watchdog;
  uint32_t poweredOff;;

  irq         = 0;
  iack        = 1;
  poweredOff  = 0;
  watchdog    = TIMEOUT;

  usleep (1000000); // 1s delay to sync threads

  while ( cpu_emulation_running ) 
  {   
    while ( ( ( value = *(gpio + 13) ) & (1 << PIN_TXN_IN_PROGRESS) ) && watchdog ) 
    {
      //NOP
      watchdog--;
    }

    if ( watchdog )
    {
      if ( ! ( value & (1 << PIN_IPL_ZERO) ) )
      {
      // if (!iack && !irq)
        if (!irq)
        {
          //if (!iack)
          //  printf ( "missed int %d\n", last_irq );

          irq = 1; 
        }
      }

      else 
      {
        if (irq)
          irq = 0;
      }  
    }

    if ( !watchdog && !poweredOff )
    {
      DEBUG_PRINTF ( "[ATARI] Powered OFF / Unresponsive\n" );
      poweredOff  = 1;
    }

    else if ( poweredOff && watchdog )
    {
      DEBUG_PRINTF ( "[ATARI] Powered ON\n" );
      poweredOff  = 0;
      do_reset    = 1;
    }

    watchdog      = TIMEOUT;
  }

  return args;
}
#endif


static inline void m68k_execute_bef ( m68ki_cpu_core *state, int num_cycles )
{
  static address_translation_cache *junk;
	/* eat up any reset cycles */
  
	if (RESET_CYCLES) 
  {
	    int rc = RESET_CYCLES;
	    RESET_CYCLES = 0;
	    num_cycles -= rc;
	    if (num_cycles <= 0)
		return;
	}
  
	/* Set our pool of clock cycles available */
	SET_CYCLES(num_cycles);
	m68ki_initial_cycles = num_cycles;

	/* Make sure we're not stopped */
	if ( !CPU_STOPPED )
	{
    //m68ki_use_data_space (); /* although this works here, with 68000 the blitter is not enabled ???

		/* Main loop.  Keep going until we run out of clock cycles */
		do
		{
      m68ki_use_data_space ();

			/* Record previous program counter */
			REG_PPC = REG_PC;

			/* Read an instruction and call its handler */
			REG_IR = m68ki_read_imm_16 ( state );
      //REG_IR = m68ki_read_imm16_addr_slowpath ( state, REG_PC, junk );

			m68ki_instruction_jump_table [REG_IR] (state);

			if ( g_buserr ) 
      {
        //printf ( "BUS ERROR - REG_PC 0x%X, REG_PPC 0x%X\n", REG_PC, REG_PPC );

        /* Record previous D/A register state (in case of bus error) */
        for ( int i = 0; i < 16; i++ )
          REG_DA_SAVE[i] = REG_DA[i];

        m68ki_exception_bus_error ( state ); 

        g_buserr = 0;	
			}

			USE_CYCLES ( CYC_INSTRUCTION [REG_IR] );
		} 
    while ( GET_CYCLES() > 0 );//&& !g_irq );

		/* set previous PC to current PC for the next entry into the loop */
		REG_PPC = REG_PC;
	}

	else
		SET_CYCLES(0);

	return;
}



extern const char *cpu_types[];

#ifdef DISASSEMBLE
char disasm_buf[4096];
#endif

void *cpu_task() 
{
	m68ki_cpu_core *state = &m68ki_cpu;
  state->gpio = gpio;
	m68k_pulse_reset(state);
  g_buserr = 0;	
  int debug = 0;

cpu_loop:
  
#ifdef DISASSEMBLE
    if ( m68k_get_reg(NULL, M68K_REG_PC) == 0x00e00f98 && debug == 0 ) 
    {
      debug = 1;
    }

    if ( debug )
    {
      m68k_disassemble(disasm_buf, m68k_get_reg(NULL, M68K_REG_PC), cpu_type);
        printf("REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3), \
          m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5), m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
        printf("REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3), \
          m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5), m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
        printf("%.8X (%.8X)]] %s\n", m68k_get_reg(NULL, M68K_REG_PC), (m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF), disasm_buf);
      printf ( "\n");
    }
    m68k_execute_bef ( state, 5 );
    
#else
		m68k_execute_bef ( state, loop_cycles );
#endif

    if ( g_irq )
    {
      //m68ki_check_interrupts ( state );

      uint16_t status = ps_read_status_reg ();

      if ( status & 0x2 ) 
      {
        M68K_END_TIMESLICE;

        DEBUG_PRINTF ( "[CPU] Emulation reset\n");

        usleep ( 1000000 ); 

        m68k_pulse_reset ( state );
      }

      last_irq = status >> 13;
      
      if ( last_irq != 0 && last_irq != last_last_irq ) 
      {
        last_last_irq = last_irq;
        m68k_set_irq ( last_irq );
      }
      
      m68ki_check_interrupts ( state );
    }

    else if ( !g_irq && last_last_irq != 0 ) 
    {
      m68k_set_irq ( 0 );

      last_last_irq = 0;
    }
  
#ifdef DISASSEMBLE
    if ( debug )
      usleep ( 250000 );
#endif
  goto cpu_loop;

stop_cpu_emulation:
  DEBUG_PRINTF ("[CPU] End of CPU thread\n");
  return (void *)NULL;
}


void sigint_handler(int sig_num) 
{
  DEBUG_PRINTF ( "\n[MAIN] Exiting\n" );
  
  if (mem_fd)
    close(mem_fd);

  if (cfg->platform->shutdown) {
    cfg->platform->shutdown(cfg);
  }

  while (!emulator_exiting) {
    emulator_exiting = 1;
    usleep(0);
  }

  exit(0);
}



extern void rtgInit ( void );
extern void *rtgRender ( void* );

int main ( int argc, char *argv[] ) 
{
  int g;
  const struct sched_param priority = {99};
#ifdef RTG
  int err;
  pthread_t rtg_tid;
  RTG_enabled = 0;
#endif

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
      printf ( "[CFG] Plaform not specified - using atari st\n" );
    }

    cfg->platform->platform_initial_setup ( cfg );
  }

  signal ( SIGINT, sigint_handler );

  mlockall ( MCL_CURRENT );  // lock in memory to keep us from paging out

  //sched_setscheduler ( 0, SCHED_FIFO, &priority );

  InitIDE ();
  #ifdef RTG
  rtgInit ();
  #endif

  ps_setup_protocol ();
  ps_reset_state_machine ();
  ps_pulse_reset ();
  usleep (1500);

  m68k_init ();
	m68k_set_cpu_type ( &m68ki_cpu, cpu_type );
  m68k_set_int_ack_callback ( &cpu_irq_ack );
  cpu_pulse_reset ();

  fc = 6;
  g_buserr = 0;

  //pthread_t ipl_tid = 0, cpu_tid, ide_tid, misc_tid;

  //int err;

  //if ( ipl_tid == 0 ) 
  //{
    //err = pthread_create ( &ipl_tid, NULL, &ipl_task, NULL );

    //if ( err != 0 )
    //  printf ( "[ERROR] Cannot create IPL thread: [%s]", strerror (err) );

    //else 
    //{
    //  pthread_setname_np ( ipl_tid, "pistorm: ipl" );
    //  printf ( "[MAIN] IPL thread created successfully\n" );
    //}
  //}

  // create cpu task

 // err = pthread_create ( &cpu_tid, NULL, &cpu_task, NULL );

  //if ( err != 0 )
  //  printf ( "[ERROR] Cannot create CPU thread: [%s]", strerror (err) );

  //else 
  //{
  //  pthread_setname_np ( cpu_tid, "pistorm: cpu" );
  //  printf ( "[MAIN] CPU thread created successfully\n" );
  //}

#ifdef MISC_TASK
  // create miscellaneous task

  err = pthread_create ( &misc_tid, NULL, &misc_task, NULL );

  if ( err != 0 )
    DEBUG_PRINTF ( "[ERROR] Cannot create MISCELLANEOUS thread: [%s]", strerror (err) );

  else 
  {
    pthread_setname_np ( misc_tid, "pistorm: misc" );
    DEBUG_PRINTF ( "[MAIN] MISCELLANEOUS thread created successfully\n" );
  }
#endif

#ifdef RTG
  // create rtg task
  if (  RTG_enabled )
  {
    err = pthread_create ( &rtg_tid, NULL, &rtgRender, NULL );

    if ( err != 0 )
      DEBUG_PRINTF ( "[ERROR] Cannot create RTG thread: [%s]", strerror (err) );

    else 
    {
      pthread_setname_np ( rtg_tid, "pistorm: rtg" );
      DEBUG_PRINTF ( "[MAIN] RTG thread created successfully\n" );
    }
  }
#endif

  /* cryptodad optimisation - .cfg no mappings */
  if ( cfg->mapped_high == 0 && cfg-> mapped_low == 0 )
    passthrough = 1;
  
  else
    passthrough = 0;

  cpu_emulation_running = 1;

  DEBUG_PRINTF ( "[MAIN] Emulation Running [%s]\n", cpu_types [cpu_type - 1] );

  if ( passthrough )
    DEBUG_PRINTF ( "[MAIN] %s Native Performance\n", cpu_types [cpu_type - 1] );

  DEBUG_PRINTF ( "\n" );

  cpu_task ();

  if ( load_new_config == 0 )
    DEBUG_PRINTF ("[MAIN] All threads appear to have concluded; ending process\n");

  if ( mem_fd )
    close ( mem_fd );

  if ( cfg->platform->shutdown )
    cfg->platform->shutdown(cfg);

  return 0;
}


void cpu_pulse_reset ( void ) 
{
  ps_pulse_reset ();
}



static unsigned int target = 0;
static uint32_t platform_res, rres;
unsigned int garbage = 0;


/* return 24 bit address */
static inline unsigned int check_ff_st( unsigned int add ) 
{
	if( ( add & 0xFF000000 ) == 0xFF000000 ) 
    add &= 0x00FFFFFF;

	return add;
}




/* levels 2 and 4 are video syncs, so thousands are coming in */
uint16_t cpu_irq_ack ( int level ) 
{
  static uint32_t ack;
  static uint8_t vec;

  fc  = 0x7; // CPU interrupt acknowledge
  ack = 0x00fffff0 | (level << 1);
  vec = ps_read_16 ( (t_a32)ack );
  
  if ( level == 2 || level == 4 ) // autovectors
  	return 24 + level;
  
  return vec;
}




static inline int32_t platform_read_check ( uint8_t type, uint32_t addr, uint32_t *res ) 
{
  if ( IDE_IDE_enabled && (addr >= 0xfff00000 && addr < 0xfff00040) )
    addr &= 0x00ffffff;
#if ATARI_VID
  else if ( ATARI_VID_enabled && (addr >= 0xffff8200 && addr < 0xffff82c4) )
    addr &= 0x00ffffff;
#endif
  
  if ( ( addr >= cfg->mapped_low && addr < cfg->mapped_high ) )
  {
    if ( handle_mapped_read ( cfg, addr, &target, type ) != -1 ) 
    {
      *res = target;
      return 1;
    }
  }

  *res = 0;
  return 0;
}


unsigned int m68k_read_memory_8 ( unsigned int address ) 
{
  if ( platform_read_check ( OP_TYPE_BYTE, address, &platform_res ) ) 
  {
    return platform_res;
  }

  address = check_ff_st ( address );

  return ps_read_8 ( (t_a32)address );  
}


unsigned int m68k_read_memory_16 ( unsigned int address ) 
{
  if ( platform_read_check ( OP_TYPE_WORD, address, &platform_res ) ) 
  {
    return platform_res;
  }

  address = check_ff_st ( address );

  return ps_read_16 ( (t_a32)address );
}


unsigned int m68k_read_memory_32 ( unsigned int address ) 
{
  if (platform_read_check ( OP_TYPE_LONGWORD, address, &platform_res ) ) 
  {
    return platform_res;
  }

  address = check_ff_st ( address );

  return ps_read_32 ( (t_a32)address );
}




#ifdef RTG
#include "platforms/atari/atari-registers.h"

/* convert ST xRRR xGGG xBBB to RGB565 */
//#define toRGB565(d) ( (uint16_t) ( (d & 0x0f00) << 3 | (d & 0x00f0) << 2 | (d & 0x000f) ) )

/* convert STe RRRR GGGG BBBB to RGB565 */
#define toRGB565(d) ( (uint16_t) ( (d & 0x0f00) << 4 | (d & 0x00f0) << 3 | (d & 0x000f) << 1 ) )

#define SYS_VARS     0x00000420
#define SYS_VARS_TOP 0x000005b4
#define PALETTE_REGS 0xffff8240

extern volatile uint16_t RTG_PAL_MODE;
extern volatile uint8_t RTG_RES;
extern volatile int RTGresChanged;
extern volatile uint16_t RTG_PALETTE_REG [16];
#endif

static inline int32_t platform_write_check ( uint8_t type, uint32_t addr, uint32_t val ) 
{
#ifdef RTG
  if ( RTG_enabled )
  {
    /* ATARI System Variables - do before anything else */
    if ( addr >= SYS_VARS && addr < SYS_VARS_TOP )
    {
        /* check palmode word */
        if ( addr == 0x448 )
        {
            RTG_PAL_MODE = val;
        }

        /* check sshiftmd word */
        else if ( addr == 0x44c )
        {
            /* has resolution changed? */
            if ( RTG_RES != val )
            {
                RTG_RES = val;
                RTGresChanged = 1;
            }
        }

        /* check v_bas_ad long */
        else if ( addr == 0x44e )
        {
            RTG_ATARI_SCREEN_RAM = (uint32_t)val;
        }
    }

    /* Palatte Registers - 16 off */
    if ( addr >= PALETTE_REGS && addr < PALETTE_REGS + 0x20 )
    {
        RTG_PALETTE_REG [ (addr - PALETTE_REGS) >> 1 ] = toRGB565 ( (uint16_t)val );
        //printf ( "palette change - REG %d = 0x%X to RGB565 0x%X\n", (addr - PALETTE_REGS) / 2, (uint16_t)value, toRGB565 ( (uint16_t)value)  );
    }
  }
  if ( RTG_enabled && (addr >= RTG_ATARI_SCREEN_RAM && addr < (RTG_ATARI_SCREEN_RAM + 0x8000)) )
  {
    rtg ( type, addr, val );
  }
#endif 

  if ( IDE_IDE_enabled && (addr >= 0xfff00000 && addr < 0xfff00040) )
      addr &= 0x00ffffff;

#if ATARI_VID
  else if ( ATARI_VID_enabled && (addr >= 0xffff8200 && addr < 0xffff82c4) )
    addr &= 0x00ffffff;
#endif

  if ( ( addr >= cfg->mapped_low && addr < cfg->mapped_high ) ) 
  {
    if ( handle_mapped_write ( cfg, addr, val, type ) != -1 ) 
      return 1;
  }

  return 0;
}



void m68k_write_memory_8 ( unsigned int address, unsigned int value ) 
{
  if (platform_write_check ( OP_TYPE_BYTE, address, value ) )
    return;
   
  address = check_ff_st ( address );

  ps_write_8 ( (t_a32)address, value );
}


void m68k_write_memory_16 ( unsigned int address, unsigned int value ) 
{
  if (platform_write_check ( OP_TYPE_WORD, address, value ) )
    return;

  address = check_ff_st ( address );

  ps_write_16 ( (t_a32)address, value );
}


void m68k_write_memory_32 ( unsigned int address, unsigned int value ) 
{
  if ( platform_write_check ( OP_TYPE_LONGWORD, address, value ) )
    return;

  address = check_ff_st ( address );

  ps_write_32 ( (t_a32)address, value );
}


void cpu_set_fc ( unsigned int _fc ) 
{
	fc = _fc;
}



#if (0)
/* cryptodad IDE */

/* ATARI IDE interface expected @ 0x00f00000 */
/* ATARI MFP (68901) @ 0x00fffa00 */
/* MFP active edge register (0x00fffa03) - bit 5 = FDC/HDC interrupt */
/* MFP interrupt enable register B (0x00fffa09) - bit 7 = FDC/HDC */
/* MFP interrupt pending register B (0x00fffa0d) - bit 7 */
void *
ide_task ( void* vptr )
{
  uint32_t ideAddress = 0x00f00000;
  uint32_t mappedAddress = ideAddress - cfg->mapped_low;
  uint16_t *cmd;

  usleep (1000000);

  //cmd = (uint16_t*)lpcmd;

  for ( int n = 0; n < 8; n++ )
  {
    if ( cfg->map_type [n] == MAPTYPE_REGISTER )
    {
      //printf ( "IDE mapping found - index %d\n", n );
      //printf ( "mapped address is 0x%x\n", cfg->mapped_low );
      //printf ( "mapped data is 0x%x\n", cfg->map_data [n] );
      break;
    }
  }

  while (1)
  {
    //if ( *cmd )
    {
     // printf ( "ide_task: cmd add 0x%x\n", *cmd );
      usleep (10000);
    }

    usleep (1);
  }
}



void *
misc_task ( void* vptr )
{
  //uint32_t ideAddress = 0x00f00000;
  //uint32_t vidAddress = 0x00ff8200;
  //uint32_t mappedAddress = ideAddress - cfg->mapped_low;
  //uint16_t *cmd;
  //uint32_t vidbase = 0x0;
  int c;
  int d;
  int doit = 0;

  usleep (1000000);
  //cmd = (uint16_t*)lpcmd;

  ///for ( int n = 0; n < 8; n++ )
  //{
  //  if ( cfg->map_type [n] == MAPTYPE_REGISTER )
  //  {
      //printf ( "IDE mapping found - index %d\n", n );
      //printf ( "mapped address is 0x%x\n", cfg->mapped_low );
      //printf ( "mapped data is 0x%x\n", cfg->map_data [n] );
  //    break;
  //  }
  //}


  while (cpu_emulation_running)
  {
    read ( STDIN_FILENO, &c, 1 );
    
    if ( c == 'v' )
    {
      uint32_t vBase;
      uint8_t vMode;

      doit = 1;

      while ( doit ) 
      {
        if ( ATARI_VID_enabled && canpeek )
        {
          peeking = 1;

          vBase = ps_read_32 ( (t_a32)((uint32_t)0x44e) );
          vMode = ps_read_16 ( (t_a32)((uint32_t)0x44c) ) >> 8;

          peeking = 0;
          doit = 0;
        }
      }

      DEBUG_PRINTF ( "Video Base Address is 0x%08x\n", vBase );
      DEBUG_PRINTF ( "Video Mode is %s\n", vMode == 1 ? "640x200" : vMode == 2 ? "640x400" : "320x200" );
      //DEBUG_PRINTF ( "Video Sync Mode is %s\n", m68k_read_memory_8 ( vidAddress + 0x0a ) & 0x02 ? "60 Hz" : "50 Hz" );
    }

    else if ( c == 'g' )
    {
      uint8_t screenGrab [32000];
      uint32_t n = 0;
      FILE *fp;

      fp = fopen ( "screendump", "w+" );

      doit = 1;

      while ( doit ) 
      {
        if ( ATARI_VID_enabled && canpeek )
        {
          peeking = 1;

          for ( n = 0; n < 32000; n++ )
            screenGrab [n] = ps_read_8 ( (t_a32)((uint32_t)0x3f8000 + n) );

          peeking = 0;
          doit = 0;
        }
      }

      fwrite ( screenGrab, 32000, 1, fp );

      //DEBUG_PRINTF ( "screen grabbed\n" );
    }
  }
}
#endif