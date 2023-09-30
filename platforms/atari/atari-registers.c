// SPDX-License-Identifier: MIT

#include "IDE.h"
#include "config_file/config_file.h"
#include "atari-registers.h"
#include "gpio/ps_protocol.h"
#include <stdbool.h>
#include "et4000.h"

uint8_t atari_rtc_emulation_enabled = 1;
//extern uint8_t IDE_emulation_enabled;
//extern uint8_t Blitter_enabled;
extern uint8_t IDE_IDE_enabled;

extern volatile uint32_t RTG_ATARI_SCREEN_RAM;
extern volatile uint8_t RTG_RES;
extern volatile int RTGresChanged;
extern int RTG_enabled;
extern volatile uint16_t RTG_PAL_MODE;
extern volatile uint16_t RTG_PALETTE_REG [16];

void configure_rtc_emulation_atari(uint8_t enabled) {
    if (enabled == atari_rtc_emulation_enabled)
        return;

    atari_rtc_emulation_enabled = enabled;
    printf("Atari RTC emulation is now %s.\n", (enabled) ? "enabled" : "disabled");
}

int handle_register_read_atari ( uint32_t addr, unsigned char type, uint32_t *val ) 
{
    //printf ("%s IDE read\n", __func__ );
        
    if ( IDE_IDE_enabled ) 
    {
        if ( addr >= IDEBASE && addr < IDETOP ) 
        {
            //printf ("IDE read 0x%X\n", addr );
            switch(type) 
            {
                case OP_TYPE_BYTE:
                    *val = readIDEB ( addr );
                    return 1;
                    break;
                case OP_TYPE_WORD:
                    *val = readIDE ( addr );
                    return 1;
                    break;
                case OP_TYPE_LONGWORD:
                    *val = readIDEL ( addr );
                    return 1;
                    break;
                case OP_TYPE_MEM:
                    return -1;
                    break;
            }
        }
    }

   // if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGBASE + 0x8000 )
   // {
    //    return et4000read ( addr, val, type );
        //printf ( "val = 0x%X\n", *val );
        //return 1;
    //}
/*
    else if ( addr >= BLITTERBASE && addr < (BLITTERBASE + BLITTERSIZE) ) 
    {
        if (Blitter_enabled) 
        {
            *val = ps_read_8 ( (t_a32)( (uint32_t)0x00ff8a3c ) );
            return 1;
        }
    }
*/
    return -1;
}





int handle_register_write_atari ( uint32_t addr, unsigned int value, unsigned char type) 
{
    //printf ( "%s REGISTER write address 0x%X, value 0x%X\n", __func__, addr, value );

    if ( IDE_IDE_enabled ) 
    {
        if ( addr >= IDEBASE && addr < IDETOP ) 
        {
            //printf ( "IDE write\n" );
            switch ( type ) 
            {
                case OP_TYPE_BYTE:
                    writeIDEB ( addr, value);
                    return 1;
                    break;
                case OP_TYPE_WORD:
                    writeIDE ( addr, value);
                    return 1;
                    break;
                case OP_TYPE_LONGWORD:
                    writeIDEL ( addr, value);
                    return 1;
                    break;
                case OP_TYPE_MEM:
                    return -1;
                    break;
            }
        }
    }

    //if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGBASE + 0x8000 )
    //{
    //    return et4000write ( addr, value, type );
    //}

    return -1;
}
