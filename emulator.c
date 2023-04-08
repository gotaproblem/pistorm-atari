// SPDX-License-Identifier: MIT

#include "m68k.h"
#include "emulator.h"
#include "platforms/platforms.h"
#include "m68kcpu.h"

#include "platforms/atari/IDE.h"
#include "platforms/atari/idedriver.h"
#include "platforms/atari/hunk-reloc.h"
#include "platforms/atari/piscsi/piscsi.h"
#include "platforms/atari/piscsi/piscsi-enums.h"
#include "platforms/atari/pistorm-dev/pistorm-dev.h"
#include "platforms/atari/pistorm-dev/pistorm-dev-enums.h"
#include "gpio/ps_protocol.h"

#include <assert.h>
#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "m68kops.h"


void *ide_task ( void* );


extern uint8_t IDE_IDE_enabled;
extern volatile unsigned int *gpio;
extern uint8_t fc;

uint8_t emulator_exiting = 0;
volatile uint32_t old_level;

volatile uint32_t last_irq = 8;
volatile uint32_t last_last_irq = 8;
volatile uint32_t iack = 0;
volatile int g_buserr = 0;

uint8_t end_signal = 0, load_new_config = 0;

int mem_fd;
int mem_fd_gpclk;
volatile int irq;
//unsigned int ovl;
volatile int cpu_emulation_running = 0;
volatile int passthrough = 0;


#define MUSASHI_HAX

#ifdef MUSASHI_HAX
#include "m68kcpu.h"
extern m68ki_cpu_core m68ki_cpu;
extern int m68ki_initial_cycles;
extern int m68ki_remaining_cycles;

#define M68K_SET_IRQ(i) old_level = CPU_INT_LEVEL; \
	CPU_INT_LEVEL = (i << 8); \
	if(old_level != 0x0700 && CPU_INT_LEVEL == 0x0700) \
		m68ki_cpu.nmi_pending = TRUE;
#define M68K_END_TIMESLICE 	m68ki_initial_cycles = GET_CYCLES(); \
	SET_CYCLES(0);
#else
#define M68K_SET_IRQ m68k_set_irq
#define M68K_END_TIMESLICE m68k_end_timeslice()
#endif

#define NOP asm("nop"); asm("nop"); asm("nop"); asm("nop");

//#define DEBUG_EMULATOR
//#ifdef DEBUG_EMULATOR
//#define DEBUG printf
//#else
//#define DEBUG(...)
//#endif

// Configurable emulator options
unsigned int cpu_type = M68K_CPU_TYPE_68000;
unsigned int loop_cycles = 20, irq_status = 0;
struct emulator_config *cfg = NULL;

volatile uint32_t do_reset=0;
volatile uint32_t gotIntLevel;
volatile uint32_t ipl;

void call_berr(uint16_t status, uint32_t address, uint mode);
static inline void m68k_execute_bef(m68ki_cpu_core *, int);





volatile uint32_t g_vector;


extern volatile unsigned int *gpio;
uint16_t irq_delay = 0;
#define TIMEOUT 1000000ul; // arbitary value, so long as it's big enough

#define ORIGINAL


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
      NOP
      watchdog--;
    }

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

    if ( !watchdog && !poweredOff )
    {
      printf ( "[ATARI] Powered OFF / Unresponsive\n" );
      poweredOff  = 1;
    }

    else if ( poweredOff && watchdog )
    {
      printf ( "[ATARI] Powered ON\n" );
      poweredOff  = 0;
      do_reset    = 1;
    }

    watchdog      = TIMEOUT;

  }

  return args;
}



static inline void m68k_execute_bef ( m68ki_cpu_core *state, int num_cycles )
{
	/* eat up any reset cycles */
	if (RESET_CYCLES) {
	    int rc = RESET_CYCLES;
	    RESET_CYCLES = 0;
	    num_cycles -= rc;
	    if (num_cycles <= 0)
		return;
	}

	/* Set our pool of clock cycles available */
	SET_CYCLES(num_cycles);
	m68ki_initial_cycles = num_cycles;

  /* See if interrupts came in */
    //if (irq)
	    m68ki_check_interrupts (state);

	/* Make sure we're not stopped */
	if ( !CPU_STOPPED )
	{
    //m68ki_check_bus_error_trap();

		/* Main loop.  Keep going until we run out of clock cycles */
		do
		{
			/* Set the address space for reads */
			m68ki_use_data_space(); /* auto-disable (see m68kcpu.h) */

			/* Record previous program counter */
			REG_PPC = REG_PC;

			g_buserr = 0;	

			/* Read an instruction and call its handler */
			REG_IR = m68ki_read_imm_16 (state);

			m68ki_instruction_jump_table [REG_IR] (state);

			if ( g_buserr ) 
      {
        /* Record previous D/A register state (in case of bus error) */
        for ( int i = 0; i < 16; i++ )
          REG_DA_SAVE[i] = REG_DA[i];

        m68ki_exception_bus_error(state);
			}

			USE_CYCLES ( CYC_INSTRUCTION [REG_IR] );
		} 
    while ( GET_CYCLES() > 0 );//&& !irq );
    /* cryptodad - really need a solution to handling interrupts better */

		/* set previous PC to current PC for the next entry into the loop */
		REG_PPC = REG_PC;
	}
	else
		SET_CYCLES(0);

	/* return how many clocks we used */
	return;
}




/* cryptodad */
int firstPass = 1;
extern const char *cpu_types[];
unsigned int ovl;
uint32_t do_disasm = 0;
char disasm_buf[4096];

#ifdef ORIGINAL

void *cpu_task() {
	m68ki_cpu_core *state = &m68ki_cpu;
 // state->ovl = ovl;
  state->gpio = gpio;
	m68k_pulse_reset(state);

cpu_loop:

  if (cpu_emulation_running) 
  {
		if (irq)
    {
      if ( cpu_type != M68K_CPU_TYPE_68000 )
			  m68k_execute_bef ( state, 18 );

      else if ( cpu_type == M68K_CPU_TYPE_68000 )
			  m68k_execute_bef ( state, 48 );
    }
		else
			m68k_execute_bef ( state, loop_cycles );
  }

  if (irq) {
    int status = ps_read_status_reg();
    if( status & 0x2 ) {
          printf("ST Reset is down...\n");
          do_reset=1;
          M68K_END_TIMESLICE;
          irq = 0;
    }

    last_irq = ( status & 0xe000) >> 13;
    
    if (last_irq != 0 && last_irq != last_last_irq) {
      last_last_irq = last_irq;
      M68K_SET_IRQ(last_irq);
    }
  }
  if (!irq && last_last_irq != 0) {
    M68K_SET_IRQ(0);
    last_last_irq = 0;
  }

  if (do_reset) {
//    cpu_pulse_reset(); 
    do_reset=0;
    usleep(2000000); // 2sec -- why not?
    //rtg_on=0;
    m68k_pulse_reset(state);
    printf("CPU emulation reset.\n");
  }

  

  if (load_new_config) {
    printf("[CPU] Loading new config file.\n");
    goto stop_cpu_emulation;
  }

  if (end_signal)
	  goto stop_cpu_emulation;

  goto cpu_loop;

stop_cpu_emulation:
  printf("[CPU] End of CPU thread\n");
  return (void *)NULL;
}

#else
void *cpu_task() 
{
  m68ki_cpu_core *state = &m68ki_cpu;

  usleep (1000000);
	
  //state->ovl = ovl;
  state->gpio = gpio;
	m68k_pulse_reset ( state );

  /* cryptodad TODO */
  if ( firstPass && m68ki_cpu.cpu_type == CPU_TYPE_000 )
  {
    firstPass = 0;
    //ps_config ( PS_CNF_CPU ); /* TODO - write config data to cpld */
  }
  
  //if ( loop_cycles ) 
  //{
  //  loop_cycles = (loop_cycles / 4) * 4;
  //}

  //else
  //{
    if ( cpu_type == M68K_CPU_TYPE_68000 )
      loop_cycles = 48;

    else if ( cpu_type == M68K_CPU_TYPE_68020 ) 
      loop_cycles = 16;

    else if ( cpu_type == M68K_CPU_TYPE_68030 ) 
      loop_cycles = 16;

    else if ( cpu_type == M68K_CPU_TYPE_68040 ) 
      loop_cycles = 16;

    else
      loop_cycles = 4;
  //}

  m68ki_check_bus_error_trap ();

  while ( cpu_emulation_running ) 
  {
    if (irq)
    {
      iack = 0;

       /* 
        * cryptodad - greatly enhances performance. 
        */      
      m68k_execute_bef ( state, loop_cycles  ); 
      
      gotIntLevel = ps_read_status_reg ();

      if ( gotIntLevel & STATUS_BIT_RESET ) 
      {
            do_reset  = 1;
            irq       = 0;
      }
      
      last_irq        = gotIntLevel >> 13;
      last_last_irq   = last_irq;

      if ( last_irq == 6 )
        M68K_SET_IRQ(last_irq);   

      iack = 1;
    }

   
    if ( !irq && last_last_irq != 0 ) 
    {
      M68K_SET_IRQ(0);
      last_last_irq = 0;
    }
    
    if ( do_reset ) 
    {
      do_reset = 0;

      M68K_END_TIMESLICE;

      usleep ( 1000000 ); // 1sec
      
      m68k_pulse_reset ( state );
      //cpu_pulse_reset ();

      printf ( "[CPU] Emulation reset\n" );
    }


    if (load_new_config) 
    {
      printf("[CPU] Loading new config file\n");
      break;
    }

    if (end_signal)
      break;
  }

  printf("[CPU] End of CPU thread\n");

  return (void *)NULL;
}
#endif

void sigint_handler(int sig_num) 
{
  printf("[MAIN] Received sigint %d, exiting.\n", sig_num);
  
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


int main ( int argc, char *argv[] ) 
{
  int g;
  int priority;

  ps_setup_protocol ();
  set_berr_callback ( &call_berr );
  
  fc = 6;

  // Some command line switch stuffles
  for (g = 1; g < argc; g++) {
    if (strcmp(argv[g], "--cpu_type") == 0 || strcmp(argv[g], "--cpu") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no CPU type specified.\n", argv[g]);
      } else {
        g++;
        cpu_type = get_m68k_cpu_type(argv[g]);
      }
    }
    else if (strcmp(argv[g], "--config-file") == 0 || strcmp(argv[g], "--config") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no config filename specified.\n", argv[g]);
      } else {
        g++;
        FILE *chk = fopen(argv[g], "rb");
        if (chk == NULL) {
          printf("Config file %s does not exist, please check that you've specified the path correctly.\n", argv[g]);
        } else {
          fclose(chk);
          load_new_config = 1;
          set_pistorm_devcfg_filename(argv[g]);
        }
      }
    }
  }

switch_config:
  srand ( clock() );

  ps_reset_state_machine ();
  ps_pulse_reset ();
  usleep (1500);

  if (load_new_config != 0) {
    uint8_t config_action = load_new_config - 1;
    load_new_config = 0;
    if (cfg) {
      free_config_file(cfg);
      free(cfg);
      cfg = NULL;
    }

    switch(config_action) {
      case PICFG_LOAD:
      case PICFG_RELOAD:
        cfg = load_config_file(get_pistorm_devcfg_filename());
        break;
      case PICFG_DEFAULT:
        cfg = load_config_file("default.cfg");
        break;
    }
  }

  if (!cfg) {
    printf("No config file specified. Trying to load default.cfg...\n");
    cfg = load_config_file("default.cfg");
    if (!cfg) {
      printf("Couldn't load default.cfg, empty emulator config will be used.\n");
      cfg = (struct emulator_config *)calloc(1, sizeof(struct emulator_config));
      if (!cfg) {
        printf("Failed to allocate memory for emulator config!\n");
        return 1;
      }
      memset(cfg, 0x00, sizeof(struct emulator_config));
    }
  }

  if (cfg) 
  {
    if (cfg->cpu_type) 
      cpu_type = cfg->cpu_type;

    if (cfg->loop_cycles) 
      loop_cycles = cfg->loop_cycles;

    if (!cfg->platform)
      cfg->platform = make_platform_config("none", "generic");

    cfg->platform->platform_initial_setup(cfg);
  }

  InitIDE ();

  signal ( SIGINT, sigint_handler );

  mlockall ( MCL_CURRENT );  // lock in memory to keep us from paging out

  priority = sched_get_priority_max ( SCHED_FIFO );
  //sched_setscheduler ( 0, SCHED_FIFO, &priority );
  //system("echo -1 >/proc/sys/kernel/sched_rt_runtime_us");


  ps_reset_state_machine ();
  ps_pulse_reset ();
  usleep (1500);

  m68k_init ();
	m68k_set_cpu_type ( &m68ki_cpu, cpu_type );
  m68k_set_int_ack_callback ( &cpu_irq_ack );
  cpu_pulse_reset ();

  pthread_t ipl_tid = 0, cpu_tid, ide_tid;

  int err;

  if ( ipl_tid == 0 ) 
  {
    err = pthread_create ( &ipl_tid, NULL, &ipl_task, NULL );

    if ( err != 0 )
      printf ( "[ERROR] Cannot create IPL thread: [%s]", strerror (err) );

    else 
    {
      pthread_setname_np ( ipl_tid, "pistorm: ipl" );
      printf ( "[MAIN] IPL thread created successfully\n" );
    }
  }

  // create cpu task

  err = pthread_create ( &cpu_tid, NULL, &cpu_task, NULL );

  if ( err != 0 )
    printf ( "[ERROR] Cannot create CPU thread: [%s]", strerror (err) );

  else 
  {
    pthread_setname_np ( cpu_tid, "pistorm: cpu" );
    printf ( "[MAIN] CPU thread created successfully\n" );
  }


  /* cryptodad optimisation - .cfg no mappings */
  //if ( cpu_type == M68K_CPU_TYPE_68000 && cfg->mapped_high == 0 && cfg-> mapped_low == 0 )
  if ( cfg->mapped_high == 0 && cfg-> mapped_low == 0 )
    passthrough = 1;
  
  else
    passthrough = 0;

  cpu_emulation_running = 1;

  printf ( "\n[MAIN] Emulation Running [%s]\n", cpu_types [cpu_type - 1] );

  if ( passthrough )
    printf ( "[MAIN] %s Native Performance\n", cpu_types [cpu_type - 1] );

  printf ( "\n" );

  // wait for cpu task to end before closing up and finishing
  pthread_join(cpu_tid, NULL);  

  while ( !emulator_exiting ) 
  {
    cpu_emulation_running = 0;
    emulator_exiting = 1;
    usleep (0);
  }


  if (load_new_config == 0)
    printf("[MAIN] All threads appear to have concluded; ending process\n");

  if (mem_fd)
    close(mem_fd);

  if (load_new_config != 0)
    goto switch_config;

  if (cfg->platform->shutdown) {
    cfg->platform->shutdown(cfg);
  }

  return 0;
}


void cpu_pulse_reset ( void ) 
{
	//m68ki_cpu_core *state = &m68ki_cpu;

  ps_pulse_reset ();

  //ovl = 1;
  //m68ki_cpu.ovl = 1;
  
  /* cryptodad TODO - might need this */
  //if ( cfg->platform->handle_reset )
  //  cfg->platform->handle_reset ( cfg );
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
  uint32_t ack;
  uint16_t vec;

  fc  = 0x7; // CPU interrupt acknowledge
  ack = 0xfffff0 + (level << 1);
  vec = ps_read_16 (ack) & 0x00ff;
  
  if ( level == 2 || level == 4 ) // autovectors
  	return 24 + level;
  
  return vec;
}


static inline int32_t platform_read_check ( uint8_t type, uint32_t addr, uint32_t *res ) 
{
  if ( IDE_IDE_enabled && (addr >= 0xfff00000 && addr < 0xfff00040) )
    addr &= 0x00ffffff;

  //if (ovl || (addr >= cfg->mapped_low && addr < cfg->mapped_high))
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
  if ( !passthrough )
  {
    if ( platform_read_check ( OP_TYPE_BYTE, address, &platform_res ) ) 
    {
      return platform_res;
    }

    //if ( cpu_type != M68K_CPU_TYPE_68000 )
      address = check_ff_st (address);
  }

  else if ( passthrough && cpu_type != M68K_CPU_TYPE_68000 )
  {
    address = check_ff_st (address);
  }

  return ps_read_8 (address);  
}


unsigned int m68k_read_memory_16 ( unsigned int address ) 
{
  //m68ki_cpu_core *state = &m68ki_cpu;

  if ( !passthrough )
  {
    if ( platform_read_check ( OP_TYPE_WORD, address, &platform_res ) ) 
    {
      return platform_res;
    }

    //if ( cpu_type != M68K_CPU_TYPE_68000 )
      address = check_ff_st (address);
  }

  else if ( passthrough && cpu_type != M68K_CPU_TYPE_68000 )
  {
    address = check_ff_st (address);
  }

  return ps_read_16 (address);
}


unsigned int m68k_read_memory_32 ( unsigned int address )  
{
  if ( !passthrough )
  {
    if (platform_read_check ( OP_TYPE_LONGWORD, address, &platform_res ) ) 
    {
      return platform_res;
    }

    //if ( cpu_type != M68K_CPU_TYPE_68000 )
      address = check_ff_st (address);
  }

  else if ( passthrough && cpu_type != M68K_CPU_TYPE_68000 )
  {
    address = check_ff_st (address);
  }

  return ps_read_32 (address);
}


static inline int32_t platform_write_check ( uint8_t type, uint32_t addr, uint32_t val ) 
{
  if ( IDE_IDE_enabled && (addr >= 0xfff00000 && addr < 0xfff00040) )
      addr &= 0x00ffffff;

  //if (ovl || (addr >= cfg->mapped_low && addr < cfg->mapped_high)) {
  if ( ( addr >= cfg->mapped_low && addr < cfg->mapped_high ) ) 
  {
    if ( handle_mapped_write ( cfg, addr, val, type ) != -1 ) 
      return 1;
  }

  return 0;
}

void m68k_write_memory_8 ( unsigned int address, unsigned int value ) 
{
  if ( !passthrough )
  {
    if ( platform_write_check ( OP_TYPE_BYTE, address, value ) )
      return;

    //if ( cpu_type != M68K_CPU_TYPE_68000 )
      address = check_ff_st (address);
  }

  else if ( passthrough && cpu_type != M68K_CPU_TYPE_68000 )
  {
    address = check_ff_st (address);
  }

  ps_write_8 (address, value);
}


void m68k_write_memory_16 ( unsigned int address, unsigned int value ) 
{
  if ( !passthrough )
  {
    if ( platform_write_check ( OP_TYPE_WORD, address, value ) )
      return;

    //if ( cpu_type != M68K_CPU_TYPE_68000 )
      address = check_ff_st (address);
  }

  else if ( passthrough && cpu_type != M68K_CPU_TYPE_68000 )
  {
    address = check_ff_st (address);
  }

  ps_write_16 (address, value );
}


void m68k_write_memory_32 ( unsigned int address, unsigned int value ) 
{
  if ( !passthrough )
  {
    if ( platform_write_check ( OP_TYPE_LONGWORD, address, value ) )
      return;

    //if ( cpu_type != M68K_CPU_TYPE_68000 )
      address = check_ff_st (address);
  }

  else if ( passthrough && cpu_type != M68K_CPU_TYPE_68000 )
  {
    address = check_ff_st (address);
  }

  ps_write_32 (address, value);
}


void cpu_set_fc ( unsigned int _fc ) 
{
	fc = _fc;
}


void call_berr ( uint16_t status, uint32_t address, uint mode ) 
{
  //m68ki_cpu_core *state = &m68ki_cpu;
  m68ki_aerr_address = address;
  m68ki_aerr_write_mode = mode ? MODE_READ : MODE_WRITE;

  g_buserr = 1;
}




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