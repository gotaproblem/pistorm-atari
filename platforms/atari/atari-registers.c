// SPDX-License-Identifier: MIT

#include "IDE.h"
#include "config_file/config_file.h"
#include "atari-registers.h"

uint8_t atari_rtc_emulation_enabled = 1;
extern uint8_t IDE_emulation_enabled;

void configure_rtc_emulation_atari(uint8_t enabled) {
    if (enabled == atari_rtc_emulation_enabled)
        return;

    atari_rtc_emulation_enabled = enabled;
    printf("Atari RTC emulation is now %s.\n", (enabled) ? "enabled" : "disabled");
}

int handle_register_read_atari(unsigned int addr, unsigned char type, unsigned int *val) {
    //printf ("%s IDE read\n", __func__ );
    if (IDE_emulation_enabled) {
        //if (!atari_rtc_emulation_enabled && addr >= CLOCKBASE && addr < CLOCKBASE + CLOCKSIZE)
        //    return -1;
        
        if (addr >= IDEBASE && addr < (IDEBASE + IDESIZE)) {
            switch(type) {
            case OP_TYPE_BYTE:
                *val = readIDEB(addr);
                return 1;
                break;
            case OP_TYPE_WORD:
                *val = readIDE(addr);
                return 1;
                break;
            case OP_TYPE_LONGWORD:
                *val = readIDEL(addr);
                return 1;
                break;
            case OP_TYPE_MEM:
                return -1;
                break;
            }
        }
    }
    return -1;
}

int handle_register_write_atari(unsigned int addr, unsigned int value, unsigned char type) {
    //printf ( "%s IDE write\n", __func__ );
    if (IDE_emulation_enabled) {
        //if (!atari_rtc_emulation_enabled && addr >= CLOCKBASE && addr < CLOCKBASE + CLOCKSIZE)
        //    return -1;
        if (addr >= IDEBASE && addr < (IDEBASE + IDESIZE)) {
            switch(type) {
            case OP_TYPE_BYTE:
                writeIDEB(addr, value);
                return 1;
                break;
            case OP_TYPE_WORD:
                writeIDE(addr, value);
                return 1;
                break;
            case OP_TYPE_LONGWORD:
                writeIDEL(addr, value);
                return 1;
                break;
            case OP_TYPE_MEM:
                return -1;
                break;
            }
        }
    }
    return -1;
}
