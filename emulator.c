// SPDX-License-Identifier: MIT

#include "m68k.h"
#include "emulator.h"
#include "platforms/platforms.h"
#include "input/input.h"
#include "m68kcpu.h"

#include "raylib.h"

#include "platforms/atari/IDE.h"
#include "platforms/atari/idedriver.h"
#include "platforms/amiga/amiga-registers.h"
#include "platforms/amiga/amiga-interrupts.h"
#include "platforms/amiga/rtg/rtg.h"
#include "platforms/amiga/hunk-reloc.h"
#include "platforms/amiga/piscsi/piscsi.h"
#include "platforms/amiga/piscsi/piscsi-enums.h"
#include "platforms/amiga/net/pi-net.h"
#include "platforms/amiga/net/pi-net-enums.h"
#include "platforms/amiga/ahi/pi_ahi.h"
#include "platforms/amiga/ahi/pi-ahi-enums.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev.h"
#include "platforms/amiga/pistorm-dev/pistorm-dev-enums.h"
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

#define KEY_POLL_INTERVAL_MSEC 5000

void *ide_task ( void* );


unsigned int ovl;

int kb_hook_enabled = 0;
int mouse_hook_enabled = 0;
int cpu_emulation_running = 1;
int swap_df0_with_dfx = 0;
int spoof_df0_id = 0;
int move_slow_to_chip = 0;
int force_move_slow_to_chip = 0;

uint8_t mouse_dx = 0, mouse_dy = 0;
uint8_t mouse_buttons = 0;
uint8_t mouse_extra = 0;

volatile int g_buserr = 0;

extern uint8_t IDE_int;
extern uint8_t IDE_IDE_enabled;
extern uint8_t IDE_emulation_enabled;
extern uint8_t IDE_a4k_int;
extern volatile unsigned int *gpio;
extern volatile uint16_t srdata;
extern uint8_t realtime_graphics_debug, emulator_exiting;
extern uint8_t rtg_on;
uint8_t realtime_disassembly, int2_enabled = 0;
uint32_t do_disasm = 0, old_level;
uint32_t last_irq = 8, last_last_irq = 8;

uint8_t ipl_enabled[8];

uint8_t end_signal = 0, load_new_config = 0;

char disasm_buf[4096];

//#define KICKBASE 0xF80000
//#define KICKSIZE 0x7FFFF

int mem_fd, mouse_fd = -1, keyboard_fd = -1;
int mem_fd_gpclk;
int irq;
//int IDEirq;

extern uint8_t fc;

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
#ifdef DEBUG_EMULATOR
#define DEBUG printf
#else
#define DEBUG(...)
#endif

// Configurable emulator options
unsigned int cpu_type = M68K_CPU_TYPE_68000;
unsigned int loop_cycles = 300, irq_status = 0;
struct emulator_config *cfg = NULL;
char keyboard_file[256] = "/dev/input/event1";

uint64_t trig_irq = 0, serv_irq = 0;
uint16_t irq_delay = 0;
//unsigned int amiga_reset=0, amiga_reset_last=0;
unsigned int do_reset=0;
uint32_t gotIntLevel;

void call_berr(uint16_t status, uint32_t address, uint mode);

void *ipl_task(void *args) {
  printf("[MAIN] IPL thread running\n");
  uint16_t old_irq = 0;
  uint32_t value;

  while (1) 
  {
    value = *(gpio + 13);

    if (value & (1 << PIN_TXN_IN_PROGRESS))
      //goto noppers;
      continue;

    if (!(value & (1 << PIN_IPL_ZERO)) ) // || ipl_enabled[get_atari_emulated_ipl()]  )
    {
      old_irq = irq_delay;
      //NOP
      if (!irq) 
      {
        //M68K_END_TIMESLICE;
        NOP
        irq = 1;
        gotIntLevel = value;
      }
    }

    else 
    {
      if (irq) 
      {
        if (old_irq) 
        {
          old_irq--;
        }

        else 
        {
          irq = 0;
        }
        //M68K_END_TIMESLICE;
        NOP
      }
    }

    #if 0
    if(do_reset==0)
    {
      amiga_reset=(value & (1 << PIN_RESET));
      if(amiga_reset!=amiga_reset_last)
      {
        amiga_reset_last=amiga_reset;
        if(amiga_reset==0)
        {
          printf("Amiga Reset is down...\n");
          do_reset=1;
          M68K_END_TIMESLICE;
        }
        else
        {
          printf("Amiga Reset is up...\n");
        }
      }
    }
    #endif
#if USEIDE
    if (IDE_IDE_enabled) {
      if ( IDE_int || get_ide(0)->drive[0].intrq )
        printf ( "IDE interrupt ? IDE_int = 0x%x, IDE_a4k_int = 0x%x, et_ide(0)->drive[0].intrq = 0x%x\n", IDE_int, IDE_a4k_int, get_ide(0)->drive[0].intrq );

      if (((IDE_int & 0x80) || IDE_a4k_int) && (get_ide(0)->drive[0].intrq || get_ide(0)->drive[1].intrq)) {
        //get_ide(0)->drive[0].intrq = 0;
        printf ( "IDE interrupt\n" );
        IDEirq = 1;
        M68K_END_TIMESLICE;
      }
      else
        IDEirq = 0;
    }
#endif
    
//noppers:
//    NOP //NOP NOP NOP NOP NOP NOP NOP
  
  }
  return args;
}

static inline void m68k_execute_bef(m68ki_cpu_core *state, int num_cycles)
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
	m68ki_check_interrupts(state);

	/* Make sure we're not stopped */
	if(!CPU_STOPPED)
	{
		/* Return point if we had an address error */

		m68ki_set_address_error_trap(state); /* auto-disable (see m68kcpu.h) */


		m68ki_check_bus_error_trap();
//    printf("checking bus error\n");

		/* Main loop.  Keep going until we run out of clock cycles */
		do
		{
			/* Set tracing according to T1. (T0 is done inside instruction) */
			//m68ki_trace_t1(); /* auto-disable (see m68kcpu.h) */

			/* Set the address space for reads */
			m68ki_use_data_space(); /* auto-disable (see m68kcpu.h) */

			/* Call external hook to peek at CPU */
			//m68ki_instr_hook(REG_PC); /* auto-disable (see m68kcpu.h) */

			/* Record previous program counter */
			REG_PPC = REG_PC;

			/* Record previous D/A register state (in case of bus error) */
			for (int i = 15; i >= 0; i--){
				REG_DA_SAVE[i] = REG_DA[i];
			}
      //memcpy ( state->dar_save, state->dar, sizeof (state->dar) );

			g_buserr = 0;	
			/* Read an instruction and call its handler */
			REG_IR = m68ki_read_imm_16(state);
//			printf("Read IR: %x\n",REG_IR);
			m68ki_instruction_jump_table[REG_IR](state);

			if( g_buserr ) {
				m68k_pulse_bus_error(state);
        //m68ki_exception_bus_error(state);
				//printf("Bus Err() %d cycles left\n", GET_CYCLES() );
			}

			USE_CYCLES ( CYC_INSTRUCTION [REG_IR] );

			/* Trace m68k_exception, if necessary */
			m68ki_exception_if_trace(state); /* auto-disable (see m68kcpu.h) */
		} while(GET_CYCLES() > 0);

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

void *cpu_task() {

  sleep(1);
  printf ( "\n\n[MAIN] Emulation Running [%s]\n\n", cpu_types [cpu_type - 1] );

	m68ki_cpu_core *state = &m68ki_cpu;
  state->ovl = ovl;
  state->gpio = gpio;
	m68k_pulse_reset(state);

  realtime_disassembly = 0;

  /* cryptodad */
  if ( firstPass && m68ki_cpu.cpu_type == CPU_TYPE_000 )
  {
    firstPass = 0;
    //ps_config ( PS_CNF_CPU ); /* TODO - write config data to cpld */
  }

cpu_loop:
  //if (mouse_hook_enabled) {
  //  get_mouse_status(&mouse_dx, &mouse_dy, &mouse_buttons, &mouse_extra);
  //}


  /* cryptodad */
  /*
  if ( m68k_get_reg ( NULL, M68K_REG_PC ) == 0xe05c86 ) //0x46a )
  {
    printf ( "hdv_init\n" );
  }

  if ( m68k_get_reg ( NULL, M68K_REG_PC ) == 0xe02480 ) //0x472 )
  {
    printf ( "hdv_bpb\n" );
  }

  if ( m68k_get_reg ( NULL, M68K_REG_PC ) == 0xe0215e ) //0x476 )
  {
    printf ( "hdv_rw\n" );
  }

  if ( m68k_get_reg ( NULL, M68K_REG_PC ) == 0xe02744 ) //0x47a )
  {
    printf ( "hdv_boot\n" );
  }

  if ( m68k_get_reg ( NULL, M68K_REG_PC ) == 0xe020a8 ) //0x47e )
  {
    printf ( "hdv_mediach\n" );
  }

  if ( m68k_get_reg ( NULL, M68K_REG_PC ) == 0xe096f6 ) 
  {
    printf ( "XHDI HD driver\n" );
  }
*/



#if (0)
  if( 0 && m68k_get_reg(NULL,M68K_REG_PC) == 0xFA0302 )
	  realtime_disassembly = 1;
  if( 0 && m68k_get_reg(NULL,M68K_REG_PC) == 0xE00406 )
    realtime_disassembly=1;

  if (realtime_disassembly && (do_disasm || cpu_emulation_running)) {

    m68k_disassemble(disasm_buf, m68k_get_reg(NULL, M68K_REG_PC), cpu_type);
/*
    printf("REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3), \
            m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5), m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
    printf("REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3), \
            m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5), m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
*/
    printf("%.8X (%.8X)]] %s\n", m68k_get_reg(NULL, M68K_REG_PC), (m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF), disasm_buf);
    if (do_disasm)
      do_disasm--;
	  m68k_execute_bef(state, 1);
  }
  else {
#endif
    if (cpu_emulation_running) {
		if (irq)
			m68k_execute_bef(state, 5);
		else
			m68k_execute_bef(state, loop_cycles);
    }
#if (0)
  }
#endif

  if (irq) 
  {
    
    int status = ps_read_status_reg();
    //printf ("gotIntLevel = 0x%x, status = 0x%x\n", gotIntLevel, status );
    if( status & STATUS_BIT_RESET ) 
    {
          printf("ST Reset is down...\n");
          do_reset=1;
          M68K_END_TIMESLICE;
          irq = 0;
    }

    last_irq = status >> 13;
    //uint8_t amiga_irq = amiga_emulated_ipl();
    //if (amiga_irq >= last_irq) {
    //    last_irq = amiga_irq;
    //}
#if (0)
    /* cryptodad IDE */
    //if (IDEirq && last_irq == 6) { //} && int2_enabled) {
    if (last_irq == 6)  //} && int2_enabled)
    {
      //write16 ( 0xdff09c, 0x8000 | (1 << 3) && last_irq != 2 );
      last_last_irq = last_irq;
      last_irq = 6;
      M68K_SET_IRQ(6);//(2);
    }


    if (last_irq != 0 && last_irq != last_last_irq) 
    {
      last_last_irq = last_irq;
      M68K_SET_IRQ(last_irq);
    }
#else
    last_last_irq = last_irq;
    if (last_irq == 6)
    M68K_SET_IRQ(last_irq);
#endif
  }

  if (!irq && last_last_irq != 0) 
  {
    M68K_SET_IRQ(0);
    last_last_irq = 0;
  }


  if (do_reset) 
  {
    //cpu_pulse_reset();
    do_reset=0;
    usleep(1000000); // 4sec
    rtg_on=0;
//    while(amiga_reset==0);
    m68k_pulse_reset(state);
    printf("[CPU] Emulation reset\n");
  }

#if AMIGA
  if (mouse_hook_enabled && (mouse_extra != 0x00)) {
    // mouse wheel events have occurred; unlike l/m/r buttons, these are queued as keypresses, so add to end of buffer
    switch (mouse_extra) {
      case 0xff:
        // wheel up
        queue_keypress(0xfe, KEYPRESS_PRESS, PLATFORM_AMIGA);
        break;
      case 0x01:
        // wheel down
        queue_keypress(0xff, KEYPRESS_PRESS, PLATFORM_AMIGA);
        break;
    }

    // dampen the scroll wheel until next while loop iteration
    mouse_extra = 0x00;
  }
#endif

  if (load_new_config) {
    printf("[CPU] Loading new config file\n");
    goto stop_cpu_emulation;
  }

  if (end_signal)
	  goto stop_cpu_emulation;

  goto cpu_loop;

stop_cpu_emulation:
  printf("[CPU] End of CPU thread\n");
  return (void *)NULL;
}

#if AMIGA
void *keyboard_task() {
  struct pollfd kbdpoll[1];
  int kpollrc;
  char c = 0, c_code = 0, c_type = 0;
  char grab_message[] = "[KBD] Grabbing keyboard from input layer",
       ungrab_message[] = "[KBD] Ungrabbing keyboard";

  printf("[KBD] Keyboard thread started\n");

  // because we permit the keyboard to be grabbed on startup, quickly check if we need to grab it
  if (kb_hook_enabled && cfg->keyboard_grab) {
    puts(grab_message);
    grab_device(keyboard_fd);
  }

  kbdpoll[0].fd = keyboard_fd;
  kbdpoll[0].events = POLLIN;

key_loop:
  kpollrc = poll(kbdpoll, 1, KEY_POLL_INTERVAL_MSEC);
  if ((kpollrc > 0) && (kbdpoll[0].revents & POLLHUP)) {
    // in the event that a keyboard is unplugged, keyboard_task will whiz up to 100% utilisation
    // this is undesired, so if the keyboard HUPs, end the thread without ending the emulation
    printf("[KBD] Keyboard node returned HUP (unplugged?)\n");
    goto key_end;
  }

  // if kpollrc > 0 then it contains number of events to pull, also check if POLLIN is set in revents
  if ((kpollrc <= 0) || !(kbdpoll[0].revents & POLLIN)) {
    if (cfg->platform->id == PLATFORM_AMIGA && last_irq != 2 && get_num_kb_queued()) {
      amiga_emulate_irq(PORTS);
    }
    goto key_loop;
  }

  while (get_key_char(&c, &c_code, &c_type)) {
    if (c && c == cfg->keyboard_toggle_key && !kb_hook_enabled) {
      kb_hook_enabled = 1;
      printf("[KBD] Keyboard hook enabled.\n");
      if (cfg->keyboard_grab) {
        grab_device(keyboard_fd);
        puts(grab_message);
      }
    } else if (kb_hook_enabled) {
      if (c == 0x1B && c_type) {
        kb_hook_enabled = 0;
        printf("[KBD] Keyboard hook disabled.\n");
        if (cfg->keyboard_grab) {
          release_device(keyboard_fd);
          puts(ungrab_message);
        }
      } else {
        if (queue_keypress(c_code, c_type, cfg->platform->id)) {
          if (cfg->platform->id == PLATFORM_AMIGA && last_irq != 2) {
            amiga_emulate_irq(PORTS);
          }
        }
      }
    }

    // pause pressed; trigger nmi (int level 7)
    if (c == 0x01 && c_type) {
      printf("[INT] Sending NMI\n");
      M68K_SET_IRQ(7);
    }

    if (!kb_hook_enabled && c_type) {
      if (c && c == cfg->mouse_toggle_key) {
        mouse_hook_enabled ^= 1;
        printf("Mouse hook %s.\n", mouse_hook_enabled ? "enabled" : "disabled");
        mouse_dx = mouse_dy = mouse_buttons = mouse_extra = 0;
      }
      if (c == 'r') {
        cpu_emulation_running ^= 1;
        printf("CPU emulation is now %s\n", cpu_emulation_running ? "running" : "stopped");
      }
      if (c == 'g') {
        realtime_graphics_debug ^= 1;
        printf("Real time graphics debug is now %s\n", realtime_graphics_debug ? "on" : "off");
      }
      if (c == 'R') {
        cpu_pulse_reset();
	//m68k_pulse_reset();
        printf("CPU emulation reset.\n");
      }
      if (c == 'q') {
        printf("Quitting and exiting emulator.\n");
	      end_signal = 1;
        goto key_end;
      }
      if (c == 'd') {
        realtime_disassembly ^= 1;
        do_disasm = 1;
        printf("Real time disassembly is now %s\n", realtime_disassembly ? "on" : "off");
      }
      if (c == 'D') {
        int r = get_mapped_item_by_address(cfg, 0x08000000);
        if (r != -1) {
          printf("Dumping first 16MB of mapped range %d.\n", r);
          FILE *dmp = fopen("./memdmp.bin", "wb+");
          fwrite(cfg->map_data[r], 16 * SIZE_MEGA, 1, dmp);
          fclose(dmp);
        }
      }
      if (c == 's' && realtime_disassembly) {
        do_disasm = 1;
      }
      if (c == 'S' && realtime_disassembly) {
        do_disasm = 128;
      }
    }
  }

  goto key_loop;

key_end:
  printf("[KBD] Keyboard thread ending\n");
  if (cfg->keyboard_grab) {
    puts(ungrab_message);
    release_device(keyboard_fd);
  }
  return (void*)NULL;
}
#endif

void stop_cpu_emulation(uint8_t disasm_cur) {
  M68K_END_TIMESLICE;
  if (disasm_cur) {
    m68k_disassemble(disasm_buf, m68k_get_reg(NULL, M68K_REG_PC), cpu_type);
    printf("REGA: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_A0), m68k_get_reg(NULL, M68K_REG_A1), m68k_get_reg(NULL, M68K_REG_A2), m68k_get_reg(NULL, M68K_REG_A3), \
            m68k_get_reg(NULL, M68K_REG_A4), m68k_get_reg(NULL, M68K_REG_A5), m68k_get_reg(NULL, M68K_REG_A6), m68k_get_reg(NULL, M68K_REG_A7));
    printf("REGD: 0:$%.8X 1:$%.8X 2:$%.8X 3:$%.8X 4:$%.8X 5:$%.8X 6:$%.8X 7:$%.8X\n", m68k_get_reg(NULL, M68K_REG_D0), m68k_get_reg(NULL, M68K_REG_D1), m68k_get_reg(NULL, M68K_REG_D2), m68k_get_reg(NULL, M68K_REG_D3), \
            m68k_get_reg(NULL, M68K_REG_D4), m68k_get_reg(NULL, M68K_REG_D5), m68k_get_reg(NULL, M68K_REG_D6), m68k_get_reg(NULL, M68K_REG_D7));
    printf("%.8X (%.8X)]] %s\n", m68k_get_reg(NULL, M68K_REG_PC), (m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF), disasm_buf);
    realtime_disassembly = 1;
  }

  cpu_emulation_running = 0;
  do_disasm = 0;
}

void sigint_handler(int sig_num) {
  //if (sig_num) { }
  //cpu_emulation_running = 0;

  //return;
  printf("[MAIN] Received sigint %d, exiting.\n", sig_num);
  if (mouse_fd != -1)
    close(mouse_fd);
  if (mem_fd)
    close(mem_fd);

  if (cfg->platform->shutdown) {
    cfg->platform->shutdown(cfg);
  }

  while (!emulator_exiting) {
    emulator_exiting = 1;
    usleep(0);
  }

  //printf("IRQs triggered: %lld\n", trig_irq);
  //printf("IRQs serviced: %lld\n", serv_irq);
  //printf("Last serviced IRQ: %d\n", last_last_irq);

  exit(0);
}

int main(int argc, char *argv[]) {
  int g;

  ps_setup_protocol();
  set_berr_callback( &call_berr );
  
  fc = 6;

  //const struct sched_param priority = {99};

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
    else if (strcmp(argv[g], "--keyboard-file") == 0 || strcmp(argv[g], "--kbfile") == 0) {
      if (g + 1 >= argc) {
        printf("%s switch found, but no keyboard device path specified.\n", argv[g]);
      } else {
        g++;
        strcpy(keyboard_file, argv[g]);
      }
    }
  }

switch_config:
  srand(clock());

  ps_reset_state_machine();
  ps_pulse_reset();
  usleep(1500);

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

  if (cfg) {
    if (cfg->cpu_type) cpu_type = cfg->cpu_type;
    if (cfg->loop_cycles) loop_cycles = cfg->loop_cycles;

    if (!cfg->platform)
      cfg->platform = make_platform_config("none", "generic");
    cfg->platform->platform_initial_setup(cfg);
  }

#if AMIGA
  if (cfg->mouse_enabled) {
    mouse_fd = open(cfg->mouse_file, O_RDWR | O_NONBLOCK);
    if (mouse_fd == -1) {
      printf("Failed to open %s, can't enable mouse hook.\n", cfg->mouse_file);
      cfg->mouse_enabled = 0;
    } else {
      /**
       * *-*-*-* magic numbers! *-*-*-*
       * great, so waaaay back in the history of the pc, the ps/2 protocol set the standard for mice
       * and in the process, the mouse sample rate was defined as a way of putting mice into vendor-specific modes.
       * as the ancient gpm command explains, almost everything except incredibly old mice talk the IntelliMouse
       * protocol, which reports four bytes. by default, every mouse starts in 3-byte mode (don't report wheel or
       * additional buttons) until imps2 magic is sent. so, command $f3 is "set sample rate", followed by a byte.
       */
      uint8_t mouse_init[] = { 0xf4, 0xf3, 0x64 }; // enable, then set sample rate 100
      uint8_t imps2_init[] = { 0xf3, 0xc8, 0xf3, 0x64, 0xf3, 0x50 }; // magic sequence; set sample 200, 100, 80
      if (write(mouse_fd, mouse_init, sizeof(mouse_init)) != -1) {
        if (write(mouse_fd, imps2_init, sizeof(imps2_init)) == -1)
          printf("[MOUSE] Couldn't enable scroll wheel events; is this mouse from the 1980s?\n");
      } else
        printf("[MOUSE] Mouse didn't respond to normal PS/2 init; have you plugged a brick in by mistake?\n");
    }
  }

  if (cfg->keyboard_file)
    keyboard_fd = open(cfg->keyboard_file, O_RDONLY | O_NONBLOCK);
  else
    keyboard_fd = open(keyboard_file, O_RDONLY | O_NONBLOCK);

  if (keyboard_fd == -1) {
    printf("Failed to open keyboard event source.\n");
  }

  if (cfg->mouse_autoconnect)
    mouse_hook_enabled = 1;

  if (cfg->keyboard_autoconnect)
    kb_hook_enabled = 1;
#endif
  InitIDE();


  signal(SIGINT, sigint_handler);

  ps_reset_state_machine();
  ps_pulse_reset();
  usleep(1500);

  m68k_init();
  //printf("Setting CPU type to %d.\n", cpu_type);
	m68k_set_cpu_type(&m68ki_cpu, cpu_type);
//printf ("SXB here 1\n" );
  m68k_set_int_ack_callback(&cpu_irq_ack);
//printf ("SXB here 2\n" );
  cpu_pulse_reset();
//printf ("SXB here 3\n" );

  pthread_t ipl_tid = 0, cpu_tid, kbd_tid, ide_tid;

  int err;
  if (ipl_tid == 0) {
    err = pthread_create(&ipl_tid, NULL, &ipl_task, NULL);
    if (err != 0)
      printf("[ERROR] Cannot create IPL thread: [%s]", strerror(err));
    else {
      pthread_setname_np(ipl_tid, "pistorm: ipl");
      printf("[MAIN] IPL thread created successfully\n");
    }
  }

  // create keyboard task
/*
  err = pthread_create(&kbd_tid, NULL, &keyboard_task, NULL);
  if (err != 0)
    printf("[ERROR] Cannot create keyboard thread: [%s]", strerror(err));
  else {
    pthread_setname_np(kbd_tid, "pistorm: kbd");
    printf("[MAIN] Keyboard thread created successfully\n");
  }
*/

  // create cpu task

  err = pthread_create(&cpu_tid, NULL, &cpu_task, NULL);
  if (err != 0)
    printf("[ERROR] Cannot create CPU thread: [%s]", strerror(err));
  else {
    pthread_setname_np(cpu_tid, "pistorm: cpu");
    printf("[MAIN] CPU thread created successfully\n");
  }


  /* cryptodad IDE */
  if ( IDE_IDE_enabled )
  {
    err = pthread_create ( &ide_tid, NULL, &ide_task, (void*)NULL );

    if ( err != 0 )
      printf ( "[ERROR] Cannot create IDE thread: [%s]", strerror ( err ) );

    else 
    {
      pthread_setname_np ( ide_tid, "pistorm: ide" );
      printf ( "[MAIN] IDE thread created successfully\n" );
    }
  }

//#define ATARI_GRAPHICS_CARD
#ifdef ATARI_GRAPHICS_CARD
    if(cfg->map_data[0]) {

    const int windowWidth = 640;
    const int windowHeight = 480;

    // Enable config flags for resizable window and vertical synchro
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(windowWidth, windowHeight, "raylib [core] example - window scale letterbox");
    SetWindowMinSize(320, 240);

    int gameScreenWidth = 640;
    int gameScreenHeight = 480;

    // Render texture initialization, used to hold the rendering result so we can easily resize it
    RenderTexture2D target = LoadRenderTexture(gameScreenWidth, gameScreenHeight);
    SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);  // Texture scale filter to use

    SetTargetFPS(60);                   // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

	uint8_t red, green, blue;
	uint16_t *src = (cfg->map_data[0]);
	uint16_t *dst;

	Image raylib_fb;
	raylib_fb.format = PIXELFORMAT_UNCOMPRESSED_R5G6B5;

	raylib_fb.width = 640;
	raylib_fb.height = 480;

	raylib_fb.mipmaps = 1;
	raylib_fb.data = src;

	Texture raylib_texture;
	raylib_texture = LoadTextureFromImage(raylib_fb);

	dst = malloc( raylib_fb.width * raylib_fb.height * 2 );


    // Main game loop
    uint16_t *srcptr;
    uint16_t *dstptr;
    while (!WindowShouldClose())        // Detect window close button or ESC key
    {
	srcptr = src;
	dstptr = dst;
	for( unsigned long l = 0 ; l < ( raylib_fb.width * raylib_fb.height ) ; l++ )
		*dstptr++ = be16toh(*srcptr++);
        UpdateTexture(raylib_texture, dst );

        BeginDrawing();
		DrawTexture( raylib_texture, 0, 0, WHITE );
        EndDrawing();
        //--------------------------------------------------------------------------------------
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    UnloadRenderTexture(target);        // Unload render texture

    CloseWindow();                      // Close window and OpenGL context
    //--------------------------------------------------------------------------------------
    }
#endif

  // wait for cpu task to end before closing up and finishing
  pthread_join(cpu_tid, NULL);

  if ( IDE_IDE_enabled )
    pthread_join(ide_tid, NULL);

  while (!emulator_exiting) {
    emulator_exiting = 1;
    usleep(0);
  }


  if (load_new_config == 0)
    printf("[MAIN] All threads appear to have concluded; ending process\n");

  if (mouse_fd != -1)
    close(mouse_fd);
  if (mem_fd)
    close(mem_fd);

  if (load_new_config != 0)
    goto switch_config;

  if (cfg->platform->shutdown) {
    cfg->platform->shutdown(cfg);
  }

  return 0;
}

void cpu_pulse_reset(void) {
	m68ki_cpu_core *state = &m68ki_cpu;
//printf ("SXB here 10\n" );
  ps_pulse_reset();
//printf ("SXB here 11\n" );
  ovl = 1;
  m68ki_cpu.ovl = 1;
  for (int i = 0; i < 8; i++) {
    ipl_enabled[i] = 0;
  }
//printf ("SXB here 12\n" );
  if (cfg->platform->handle_reset)
    cfg->platform->handle_reset(cfg);
//printf ("SXB here 13\n" );
//  int status = ps_read_status_reg();
#if 0
  int pin_reset = *(gpio + 13) & ( 1 << PIN_RESET );
  printf("pin_reset: %x\n", pin_reset );

  if( !pin_reset ) {
	  m68k_pulse_reset(state);
  }
#endif
}



static unsigned int target = 0;
static uint32_t platform_res, rres;
unsigned int garbage = 0;


/* levels 2 and 4 are video syncs, so thousands are coming in */
uint16_t cpu_irq_ack (int level) 
{
  fc = 0x7; // CPU interrupt acknowledge

  uint32_t ack = 0xfffff0 + (level << 1);
  uint16_t vec = ps_read_16 (ack) & 0x00ff;
  

  if( level == 2 || level == 4 ) { // autovectors
  	return 24 + level;
  }

  return vec;
}


static inline int32_t platform_read_check(uint8_t type, uint32_t addr, uint32_t *res) 
{
  //if (ovl || (addr >= cfg->mapped_low && addr < cfg->mapped_high))
  if ((addr >= cfg->mapped_low && addr < cfg->mapped_high))
  {
    if ( handle_mapped_read(cfg, addr, &target, type) != -1 ) 
    {
      *res = target;
      return 1;
    }
  }

  *res = 0;
  return 0;
}


unsigned int m68k_read_memory_8(unsigned int address) 
{
  if (platform_read_check(OP_TYPE_BYTE, address, &platform_res)) 
  {
    return platform_res;
  }

  address &= 0x00ffffff;

  return ps_read_8 (address);  
}


unsigned int m68k_read_memory_16 (unsigned int address) 
{
  if ( platform_read_check ( OP_TYPE_WORD, address, &platform_res ) ) 
  {
    return platform_res;
  }

  address &= 0x00ffffff;

  return ps_read_16 (address);
}


unsigned int m68k_read_memory_32(unsigned int address) 
{
  if (platform_read_check(OP_TYPE_LONGWORD, address, &platform_res)) 
  {
    return platform_res;
  }

  address &= 0x00ffffff;

  return ps_read_32 (address);
}


static inline int32_t platform_write_check(uint8_t type, uint32_t addr, uint32_t val) 
{
  //if (ovl || (addr >= cfg->mapped_low && addr < cfg->mapped_high)) {
  if ( (addr >= cfg->mapped_low && addr < cfg->mapped_high)) 
  {
    if (handle_mapped_write(cfg, addr, val, type) != -1) 
    {
      return 1;
    }
  }

  return 0;
}

void m68k_write_memory_8(unsigned int address, unsigned int value) 
{
  if (platform_write_check(OP_TYPE_BYTE, address, value))
    return;

  address &= 0x00ffffff;

  ps_write_8 (address, value);
}


void m68k_write_memory_16(unsigned int address, unsigned int value) 
{
  if (platform_write_check(OP_TYPE_WORD, address, value))
    return;

  address &= 0x00ffffff;

  ps_write_16 (address, value );

  return;
}


void m68k_write_memory_32(unsigned int address, unsigned int value) 
{
  if ( platform_write_check ( OP_TYPE_LONGWORD, address, value ) )
    return;

  address &= 0x00ffffff;

  ps_write_32 (address, value);
  
  return;
}


void cpu_set_fc(unsigned int _fc) {
	fc = _fc;
}

void call_berr(uint16_t status, uint32_t address, uint mode) 
{
  //if( status & STATUS_BIT_BERR ) 
  //{
    m68ki_cpu_core *state = &m68ki_cpu;
    //printf("call_berr(): fc=%d\n", fc);
    m68ki_aerr_address = address;
    m68ki_aerr_write_mode = mode ? MODE_READ : MODE_WRITE;
    g_buserr = 1;
  //}
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