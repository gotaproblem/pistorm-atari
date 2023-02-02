// SPDX-License-Identifier: MIT

#include "platforms/platforms.h"
#include "pistorm-dev/pistorm-dev-enums.h"
#include "atari-autoconf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "a314/a314.h"




uint32_t piscsi_base = 0, pistorm_dev_base = 0;
extern uint8_t *piscsi_rom_ptr;



extern void adjust_ranges_atari(struct emulator_config *cfg);




int nib_latch = 0;


