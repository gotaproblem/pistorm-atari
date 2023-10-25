// SPDX-License-Identifier: MIT

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "atari-autoconf.h"
#include "atari-registers.h"
//#include "atari-interrupts.h"
#include "gpio/ps_protocol.h"
#include "hunk-reloc.h"
//#include "net/pi-net-enums.h"
//#include "net/pi-net.h"
//#include "piscsi/piscsi-enums.h"
//#include "piscsi/piscsi.h"
//#include "ahi/pi_ahi.h"
//#include "ahi/pi-ahi-enums.h"
#include "pistorm-dev/pistorm-dev-enums.h"
#include "pistorm-dev/pistorm-dev.h"
#include "platforms/platforms.h"
//#include "platforms/shared/rtc.h"
//#include "rtg/rtg.h"
//#include "a314/a314.h"
#include "platforms/atari/atari-registers.h"

#define DEBUGPRINT 0
#if DEBUGPRINT
#define DEBUG_PRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...) ;
#endif

#ifdef DEBUG_ATARI_PLATFORM
#define DEBUG printf
#else
#define DEBUG(...)
#endif

extern int handle_register_read_atari ( uint32_t addr, unsigned char type, unsigned int *val);
extern int handle_register_write_atari ( uint32_t addr, unsigned int value, unsigned char type);

//#ifdef RTG
extern bool RTG_enabled;
extern bool screenGrab;
extern long RTG_fps;
//#endif
extern int FPU68020_SELECTED;

extern const char *op_type_names[OP_TYPE_NUM];
//extern uint8_t cdtv_mode;
extern uint8_t rtc_type;
//extern unsigned char cdtv_sram[32 * SIZE_KILO];
//extern unsigned int a314_base;

//extern int kb_hook_enabled;
//extern int mouse_hook_enabled;

//extern int swap_df0_with_dfx;
//extern int spoof_df0_id;
//extern int move_slow_to_chip;
//extern int force_move_slow_to_chip;

#define min(a, b) (a < b) ? a : b
#define max(a, b) (a > b) ? a : b

#ifdef PISCSI
uint8_t piscsi_enabled = 0;
extern uint32_t piscsi_base;
#endif
//uint8_t a314_emulation_enabled = 0, a314_initialized = 0;


extern uint32_t pistorm_dev_base;

extern void stop_cpu_emulation(uint8_t disasm_cur);

static uint32_t ac_waiting_for_physical_pic = 0;

inline int custom_read_atari(struct emulator_config *cfg, unsigned int addr, unsigned int *val, unsigned char type) {
#ifdef PISCSI
    if (piscsi_enabled && addr >= piscsi_base && addr < piscsi_base + (64 * SIZE_KILO)) {
        //DEBUG_PRINTF ("[Amiga-Custom] %s read from PISCSI base @$%.8X.\n", op_type_names[type], addr);
        //stop_cpu_emulation(1);
        *val = handle_piscsi_read(addr, type);
        return 1;
    }
#endif
    return -1;
}

inline int custom_write_atari(struct emulator_config *cfg, unsigned int addr, unsigned int val, unsigned char type) {
#ifdef PISCSI
    if (piscsi_enabled && addr >= piscsi_base && addr < piscsi_base + (64 * SIZE_KILO)) {
        //DEBUG_PRINTF ("[Amiga-Custom] %s write to PISCSI base @$%.8x: %.8X\n", op_type_names[type], addr, val);
        handle_piscsi_write(addr, val, type);
        return 1;
    }
#endif
    return -1;
}

void adjust_ranges_atari(struct emulator_config *cfg) {
    cfg->mapped_high = 0;
    cfg->mapped_low = 0;
    cfg->custom_high = 0;
    cfg->custom_low = 0;

    // Set up the min/max ranges for mapped reads/writes
    for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) {
        if (cfg->map_type[i] != MAPTYPE_NONE) {
            if ((cfg->map_offset[i] != 0 && cfg->map_offset[i] < cfg->mapped_low) || cfg->mapped_low == 0)
                cfg->mapped_low = cfg->map_offset[i];
            if (cfg->map_offset[i] + cfg->map_size[i] > cfg->mapped_high)
                cfg->mapped_high = cfg->map_offset[i] + cfg->map_size[i];
        }
    }
#ifdef PISCSI
    if (piscsi_enabled) {
        if (cfg->custom_low == 0)
            cfg->custom_low = PISCSI_OFFSET;
        else
            cfg->custom_low = min(cfg->custom_low, PISCSI_OFFSET);
        cfg->custom_high = max(cfg->custom_high, PISCSI_UPPER);
        if (piscsi_base != 0) {
            cfg->custom_low = min(cfg->custom_low, piscsi_base);
        }
    }
#endif
    if ( cfg->custom_high )
    {
        DEBUG_PRINTF ("Platform custom range: %.8X-%.8X\n", cfg->custom_low, cfg->custom_high);
        DEBUG_PRINTF ("Platform mapped range: %.8X-%.8X\n", cfg->mapped_low, cfg->mapped_high);
    }
}

int setup_platform_atari(struct emulator_config *cfg) {
    DEBUG_PRINTF ("Performing setup for Atari platform.\n");

    if (strlen(cfg->platform->subsys)) {
        DEBUG_PRINTF ("Subsystem is [%s]\n", cfg->platform->subsys);
        if (strcmp(cfg->platform->subsys, "st") == 0 ) {
            DEBUG_PRINTF ("Configuring %s TODO.\n", cfg->platform->subsys );
            //adjust_IDE_4000();
        }
        if (strcmp(cfg->platform->subsys, "ste") == 0 ) {
            DEBUG_PRINTF ("Configuring %s TODO.\n", cfg->platform->subsys );
            //adjust_IDE_4000();
        }
        else if (strcmp(cfg->platform->subsys, "mega") == 0) {
            DEBUG_PRINTF ("Configuring %s TODO.\n", cfg->platform->subsys );
            //adjust_IDE_1200();
        }
        else if (strcmp(cfg->platform->subsys, "tt") == 0) {
            DEBUG_PRINTF ("Configuring %s TODO.\n", cfg->platform->subsys );
            //adjust_IDE_1200();
        }
        else if (strcmp(cfg->platform->subsys, "falcon") == 0) {
            DEBUG_PRINTF ("Configuring %s TODO.\n", cfg->platform->subsys );
            
            //rtc_type = RTC_TYPE_MSM;
        }
    }
    else
        DEBUG_PRINTF ("No sub system specified.\n");

    int index = get_named_mapped_item(cfg, "cpu_slot_ram");
    if (index != -1) {
        m68k_add_ram_range((uint32_t)cfg->map_offset[index], (uint32_t)cfg->map_high[index], cfg->map_data[index]);
    }

    adjust_ranges_atari(cfg);

    return 0;
}

#define CHKVAR(a) (strcmp(var, a) == 0)

void setvar_atari(struct emulator_config *cfg, char *var, char *val) 
{
    if ( !var )
        return;

    char *endptr;

/*
    if CHKVAR("enable_rtc_emulation") {
        int8_t rtc_enabled = 0;
        if (!val || strlen(val) == 0)
            rtc_enabled = 1;
        else {
            rtc_enabled = get_int(val);
        }
        if (rtc_enabled != -1) {
            configure_rtc_emulation_atari(rtc_enabled);
        }
    }
*/
    if CHKVAR ( "hdd0" ) 
    {
        if ( val && strlen ( val ) != 0 )
            set_hard_drive_image_file_atari ( 0, val );
    }

    if CHKVAR ( "hdd1" ) 
    {
        if ( val && strlen ( val ) != 0 )
            set_hard_drive_image_file_atari ( 1, val );
    }

    if CHKVAR ( "hdd2" ) 
    {
        if ( val && strlen ( val ) != 0 )
            set_hard_drive_image_file_atari ( 2, val );
    }

    if CHKVAR ( "hdd3" ) 
    {
        if ( val && strlen ( val ) != 0 )
            set_hard_drive_image_file_atari ( 3, val );
    }

    if CHKVAR ( "hdd4" ) 
    {
        if ( val && strlen ( val ) != 0 )
            set_hard_drive_image_file_atari ( 4, val );
    }

    if CHKVAR ( "hdd5" ) 
    {
        if ( val && strlen ( val ) != 0 )
            set_hard_drive_image_file_atari ( 5, val );
    }

    if CHKVAR ( "hdd6" ) 
    {
        if ( val && strlen ( val ) != 0 )
            set_hard_drive_image_file_atari ( 6, val );
    }

    if CHKVAR ( "hdd7" ) 
    {
        if ( val && strlen ( val ) != 0 )
            set_hard_drive_image_file_atari ( 7, val );
    }

    if CHKVAR ( "rtg" ) 
        RTG_enabled = 1;

    if CHKVAR ( "screengrab" ) 
        screenGrab = 1;

    if CHKVAR ( "fps" )
        RTG_fps = strtol ( val, &endptr, 0 );

    /* cryptodad allow selection of fpu for 68020 CPU */
    if CHKVAR ( "68020fpu" )
        FPU68020_SELECTED = 1;
        
#ifdef PISCSI
    // PiSCSI stuff
    if (CHKVAR("piscsi") && !piscsi_enabled) {
        DEBUG_PRINTF ("[ATARI] PISCSI Interface Enabled.\n");
        piscsi_enabled = 1;
        piscsi_init();
        //add_z2_pic(ACTYPE_PISCSI, 0);
        adjust_ranges_atari(cfg);
    }
    if (piscsi_enabled) {
        if CHKVAR("piscsi0") {
            piscsi_map_drive(val, 0);
        }
        if CHKVAR("piscsi1") {
            piscsi_map_drive(val, 1);
        }
        if CHKVAR("piscsi2") {
            piscsi_map_drive(val, 2);
        }
        if CHKVAR("piscsi3") {
            piscsi_map_drive(val, 3);
        }
        if CHKVAR("piscsi4") {
            piscsi_map_drive(val, 4);
        }
        if CHKVAR("piscsi5") {
            piscsi_map_drive(val, 5);
        }
        if CHKVAR("piscsi6") {
            piscsi_map_drive(val, 6);
        }
    }
#endif
/*
    // RTC stuff
    if CHKVAR("rtc_type") {
        if (val && strlen(val) != 0) {
            if (strcmp(val, "msm") == 0) {
                DEBUG_PRINTF ("[ATARI] RTC type set to MSM.\n");
                rtc_type = RTC_TYPE_MSM;
            }
            else {
                DEBUG_PRINTF ("[ATARI] RTC type set to Ricoh.\n");
                rtc_type = RTC_TYPE_RICOH;
            }
        }
    }
*/

}

void handle_reset_atari(struct emulator_config *cfg) {
  
    ac_waiting_for_physical_pic = 0;


    DEBUG_PRINTF ("[ATARI] Reset handler.\n");
#ifdef PISCSI
    if ( piscsi_enabled )
        piscsi_refresh_drives ();
#endif

    adjust_ranges_atari ( cfg );
}

void shutdown_platform_atari(struct emulator_config *cfg) {
    DEBUG_PRINTF ("[ATARI] Performing Atari platform shutdown.\n");
    if (cfg) {}
    
    if (cfg->platform->subsys) {
        free(cfg->platform->subsys);
        cfg->platform->subsys = NULL;
    }
#ifdef PISCSI
    if (piscsi_enabled) {
        piscsi_shutdown();
        piscsi_enabled = 0;
    }
#endif
    
    ac_waiting_for_physical_pic = 0;

    autoconfig_reset_all();
    DEBUG_PRINTF ("[ATARI] Platform shutdown completed.\n");
}

void create_platform_atari ( struct platform_config *cfg, char *subsys ) 
{
    cfg->register_read          = handle_register_read_atari;
    cfg->register_write         = handle_register_write_atari;
    cfg->custom_read            = custom_read_atari;
    cfg->custom_write           = custom_write_atari;
    cfg->platform_initial_setup = setup_platform_atari;
    cfg->handle_reset           = handle_reset_atari;
    cfg->shutdown               = shutdown_platform_atari;

    cfg->setvar                 = setvar_atari;
    cfg->id                     = PLATFORM_ATARI;

    if (subsys) 
    {
        cfg->subsys = malloc ( strlen(subsys) + 1 );
        strcpy ( cfg->subsys, subsys );

        for ( unsigned int i = 0; i < strlen (cfg->subsys); i++ ) 
        {
            cfg->subsys[i] = toupper ( cfg->subsys[i] );
        }
    }
}
