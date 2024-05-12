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
extern bool IDE_enabled;
//extern int IDEIF;

extern volatile uint32_t RTG_ATARI_SCREEN_RAM;
extern volatile uint8_t RTG_RES;
extern volatile int RTGresChanged;
extern int RTG_enabled;
extern volatile uint16_t RTG_PAL_MODE;
extern volatile uint16_t RTG_PALETTE_REG [16];
extern bool ET4000Initialised;


void configure_rtc_emulation_atari(uint8_t enabled) {
    if (enabled == atari_rtc_emulation_enabled)
        return;

    atari_rtc_emulation_enabled = enabled;
    printf("Atari RTC emulation is now %s.\n", (enabled) ? "enabled" : "disabled");
}




bool first = true;

int handle_register_read_atari ( uint32_t addr, unsigned char type, uint32_t *val ) 
{
    static int res;
    static int r;
        
    if ( IDE_enabled && ( addr >= IDEBASE && addr < IDETOP ) )
    {
        res = 1;
        //printf ("IDE read 0x%X, type = %d\n", addr, type );
        //IDEIF = (addr & 0xF0) + IDEBASE;

        switch(type) 
        {
            case OP_TYPE_BYTE:
                *val = readIDEB ( addr );
                
                break;

            case OP_TYPE_WORD:
                *val = readIDE ( addr );
                
                break;

            case OP_TYPE_LONGWORD:
                *val = readIDEL ( addr );
                
                break;

            case OP_TYPE_MEM:
                res = -1;

                break;
        }

        /*
        uint8_t mfp = ps_read_8 ( 0x00FFFA03 );
        if ( !(mfp & 0x20) && first )
        {
            ps_write_8 ( 0x00FFFA03, mfp |= (1 << 5) ); // set
            first = false;
        }
        printf ( "%s mfp = 0x%X\n", __func__, mfp );
        printf ( "%s 0x00FFFA09 = 0x%X\n", __func__, ps_read_8 ( 0x00FFFA09 ) );
        printf ( "%s 0x00FFFA0D = 0x%X\n", __func__, ps_read_8 ( 0x00FFFA0D ) );
        printf ( "%s 0x11C = 0x%X\n", __func__, ps_read_8 ( 0x11C ) );
        //ps_write_8 ( 0xFFFFFA03, mfp & ~(1 << 5) ); // clear
        //ps_write_8 ( 0x00FFFA03, mfp |= (1 << 5) ); // set
        printf ( "IDE read returns 0x%X\n", *val );
        */
        return res;
    }

    if ( ET4000Initialised && ( addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_REGTOP ) )
    {
        //printf ( "calling et4000Read () with addr 0x%X\n", addr );
   
        r = et4000Read ( addr, val, type );

        return r;
    }
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
    static int res;
    //printf ( "%s REGISTER write address 0x%X, value 0x%X\n", __func__, addr, value );

    if ( IDE_enabled && ( addr >= IDEBASE && addr < IDETOP ) )
    {
        res = 1;
        //printf ("IDE write 0x%X, type = %d\n", addr, type );
        //IDEIF = (addr & 0xF0) + IDEBASE;

        switch ( type ) 
        {
            case OP_TYPE_BYTE:
                writeIDEB ( addr, value);
                
                break;

            case OP_TYPE_WORD:
                writeIDE ( addr, value);
                
                break;

            case OP_TYPE_LONGWORD:
                writeIDEL ( addr, value);
                
                break;
                
            case OP_TYPE_MEM:
                res = -1;

                break;
        }

        //uint8_t mfp = ps_read_8 ( 0x00FFFA03 );
        //if ( mfp & 0x20 )
        //printf ( "%s mfp = 0x%X\n", __func__, mfp );
        //ps_write_8 ( 0x00FFFA03, mfp & ~(1 << 5) ); // clear
        //ps_write_8 ( 0x00FFFA03, mfp |= (1 << 5) ); // set

        return res;
    }

    if ( ET4000Initialised && ( addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_REGTOP ) )
    {
        //printf ( "calling et4000Write () with addr 0x%X\n", addr );
        et4000Write ( addr, value, type );

        return 1;
    }

    return -1;
}
