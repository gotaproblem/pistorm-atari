// SPDX-License-Identifier: MIT

#include "platforms/platforms.h"
#include "pistorm-dev/pistorm-dev-enums.h"
#include "atari-autoconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include "a314/a314.h"

#define Z2_Z2      0xC
#define Z2_FAST    0x2
#define Z2_BOOTROM 0x1


uint32_t piscsi_base = 0, pistorm_dev_base = 0;
extern uint8_t *piscsi_rom_ptr;



extern void adjust_ranges_atari(struct emulator_config *cfg);

int ac_z2_current_pic = 0;
int ac_z2_pic_count = 0;
int ac_z2_done = 0;
int ac_z2_type[AC_PIC_LIMIT];
int ac_z2_index[AC_PIC_LIMIT];
unsigned int ac_base[AC_PIC_LIMIT];

int ac_z3_current_pic = 0;
int ac_z3_pic_count = 0;
int ac_z3_done = 0;
int ac_z3_type[AC_PIC_LIMIT];
int ac_z3_index[AC_PIC_LIMIT];


int nib_latch = 0;


// PiStorm Device Interaction ROM
unsigned char ac_pistorm_rom[] = {
    Z2_Z2, AC_MEM_SIZE_64KB,                // 00/01, Z2, bootrom, 64 KB
    0x6, 0xB,                               // 06/0B, product id
    0x0, 0x0,                               // 00/0a, any space where it fits
    0x0, 0x0,                               // 0c/0e, reserved
    PISTORM_AC_MANUF_ID,                    // Manufacturer ID
    0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x2, 0x2, // 18/.../26, serial
    0x4, 0x0, 0x0, 0x0,                     // Optional BOOT ROM vector
};

// PiSCSI AutoConfig Device ROM
unsigned char ac_piscsi_rom[] = {
    Z2_Z2 | Z2_BOOTROM, AC_MEM_SIZE_64KB,   // 00/01, Z2, bootrom, 64 KB
    0x6, 0xA,                               // 06/0A, product id
    0x0, 0x0,                               // 00/0a, any space where it fits
    0x0, 0x0,                               // 0c/0e, reserved
    PISTORM_AC_MANUF_ID,                    // Manufacturer ID
    0x0, 0x0, 0x0, 0x0, 0x0, 0x4, 0x2, 0x1, // 18/.../26, serial
    0x4, 0x0, 0x0, 0x0,                     // Optional BOOT ROM vector
};

void autoconfig_reset_all() {
 // printf("[AUTOCONF] Resetting all autoconf data.\n");
  for (int i = 0; i < AC_PIC_LIMIT; i++) {
    ac_z2_type[i] = ACTYPE_NONE;
    ac_z3_type[i] = ACTYPE_NONE;
    ac_z2_index[i] = 0;
    ac_z3_index[i] = 0;
  }
  ac_z3_pic_count = 0;
  ac_z2_pic_count = 0;
  ac_z2_current_pic = 0;
  ac_z3_current_pic = 0;
}