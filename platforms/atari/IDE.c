// SPDX-License-Identifier: MIT

//
//  IDE.c
//  Originally based on Omega's IDE emulation,
//  created by Matt Parsons on 06/03/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//

#include "IDE.h"
#include "idedriver.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>

#include "platforms/shared/rtc.h"
#include "config_file/config_file.h"

#include "atari-registers.h"

#define DEBUG_IDE
#ifdef DEBUG_IDE
#define DEBUG printf
#else
#define DEBUG(...)
#endif

//#define IDE_DUMMY 

#ifdef IDE_DUMMY
uint8_t *atariide0 = NULL;

uint8_t IDE_feature_w = 0, IDE_command_w = 0, IDE_sec_count = 0, IDE_sec_num = 0, IDEwrite8 = 0, IDE_cyl_hi = 0, IDE_dev_head = 0;
uint8_t IDE_devctrl_w = 0, IDE_cyl_low = 0, IDE_error_r = 0, IDE_status_r = 0, IDE_altst_r = 0, IDE_data = 0;

uint8_t IDE_read8(uint8_t *dummy, uint8_t IDE_action) { if (dummy || IDE_action) {}; return 0; }
uint16_t IDE_read16(uint8_t *dummy, uint8_t IDE_action) { if (dummy || IDE_action) {}; return 0; }

void IDE_write8(uint8_t *dummy, uint8_t IDE_action, uint8_t value) { if (dummy || IDE_action || value) {}; }
void IDE_write16(uint8_t *dummy, uint8_t IDE_action, uint16_t value) { if (dummy || IDE_action || value) {}; }
void IDE_reset_begin(uint8_t *dummy) { if (dummy) {}; }

uint8_t *IDE_allocate(const char *name) { if (name) {}; return NULL; }

void IDE_attach_hdf(uint8_t *dummy, uint32_t idx, uint32_t atarifd) {
  if (dummy || idx || atarifd) {};
  printf("[!!!IDE] No IDE emulation layer available, HDF image not attached.\n");
  return;
}

void IDE_attach(uint8_t *dummy, uint32_t idx, uint32_t atarifd) {
  if (dummy || idx || atarifd) {};
  printf("[!!!IDE] No IDE emulation layer available, image not mounted.\n");
  return;
}
#else
static struct IDE_controller *atariide0 = NULL;
#endif

//uint8_t gary_cfg[8];

//uint8_t ramsey_cfg = 0x08;
//static uint8_t ramsey_id = RAMSEY_REV7;

//int ataricounter;
//static uint8_t IDE_cs, IDE_cs_mask, //IDE_cfg; // IDE_irq
int atarifd;

//uint8_t rtc_type = RTC_TYPE_RICOH;

char *atari_image_file[IDE_MAX_HARDFILES];

//uint8_t cdtv_mode = 0;
//unsigned char cdtv_sram[32 * SIZE_KILO];

//uint8_t IDE_a4k = 0xA0;
//uint16_t IDE_a4k_irq = 0;
//uint8_t IDE_a4k_int = 0;
//uint8_t IDE_int = 0;

//uint32_t IDE_IDE_mask = ~GDATA;
//uint32_t IDE_IDE_base = GDATA;
uint8_t IDE_IDE_enabled = 1;
uint8_t IDE_emulation_enabled = 1;
//uint8_t IDE_IDE_adj = 0;

struct ide_controller *get_ide(int index) {
  if (index) {}
  return atariide0;
}

//void adjust_IDE_4000() {
  //IDE_IDE_base = IDE_IDE_BASE_A4000;
  //IDE_IDE_adj = 2;
  //IDE_a4k_int = 1;
  //printf ("IDE add set to 0x%x\n", IDE_IDE_base );
//}

//void adjust_IDE_1200() {
//}

void set_hard_drive_image_file_atari(uint8_t index, char *filename) {
  if (atari_image_file[index] != NULL)
    free(atari_image_file[index]);
  atari_image_file[index] = calloc(1, strlen(filename) + 1);
  strcpy(atari_image_file[index], filename);
}

void InitIDE (void) 
{
  uint8_t num_IDE_drives = 0;

  for (int i = 0; i < IDE_MAX_HARDFILES; i++) 
  {
    if (atari_image_file[i]) 
    {
      atarifd = open(atari_image_file[i], O_RDWR);

      if (atarifd != -1) 
      {
        if (!atariide0)
            atariide0 = IDE_allocate("cf");
      }

      if (atarifd == -1) 
      {
        printf("[HDD%d] HDD Image %s failed open\n", i, atari_image_file[i]);
      } 
      
      else 
      {
        if ( strcmp ( atari_image_file [i] + ( strlen (atari_image_file [i] ) - 2 ), "ST" ) == 0 )
        {
          printf ( "[FDD%d] Attaching FDD image %s.\n", i, atari_image_file [i] );

          IDE_attach_st ( atariide0, i, atarifd );
          num_IDE_drives++;

          printf ( "[FDD%d] FDD Image %s attached\n", i, atari_image_file [i] );
        }

        else if ( strcmp ( atari_image_file [i] + ( strlen (atari_image_file [i] ) - 3 ), "img" ) == 0 )
        {
          printf("[HDD%d] Attaching HDD image %s.\n", i, atari_image_file[i]);
          //if (strcmp(atari_image_file[i] + (strlen(atari_image_file[i]) - 3), "img") != 0) {
            //printf("No header present on HDD image %s.\n", atari_image_file[i]);
            IDE_attach_hdf ( atariide0, i, atarifd );
            num_IDE_drives++;
          //}
          //else {
          //  printf("Attaching HDD image with header.\n");
          //  IDE_attach(atariide0, i, atarifd);
          //  num_IDE_drives++;
          //}
          printf("[HDD%d] HDD Image %s attached\n", i, atari_image_file[i]);
        }
      }
    }
  }

  if (atariide0)
    IDE_reset_begin (atariide0);

  if (num_IDE_drives == 0) 
  {
    // No IDE drives mounted, disable IDE component of IDE
    printf("No IDE drives mounted, disabling IDE component.\n");
    IDE_IDE_enabled = 0;
  }
}

static uint8_t IDE_action = 0;

void writeIDEB(unsigned int address, unsigned int value) 
{
  if (atariide0) 
  {
    //if (address >= IDEBASE && address < (IDEBASE + IDESIZE) ) {
#if (1)
      switch ((address - IDEBASE)){//IDE_IDE_base) - IDE_IDE_adj) {
        case GFEAT_OFFSET:
          //printf("Write to GFEAT: %.2X.\n", value);
          IDE_action = IDE_feature_w;
          goto IDEwrite8;
        case GCMD_OFFSET:
          //printf("Write to GCMD: %.2X.\n", value);
          IDE_action = IDE_command_w;
          goto IDEwrite8;
        case GSECTCOUNT_OFFSET:
          IDE_action = IDE_sec_count;
          goto IDEwrite8;
        case GSECTNUM_OFFSET:
          IDE_action = IDE_sec_num;
          goto IDEwrite8;
        case GCYLLOW_OFFSET:
          IDE_action = IDE_cyl_low;
          goto IDEwrite8;
        case GCYLHIGH_OFFSET:
          IDE_action = IDE_cyl_hi;
          goto IDEwrite8;
        case GDEVHEAD_OFFSET:
          //printf("Write to GDEVHEAD: %.2X.\n", value);
          IDE_action = IDE_dev_head;
          goto IDEwrite8;
        case GCTRL_OFFSET:
          //printf("Write to GCTRL: %.2X.\n", value);
          IDE_action = IDE_devctrl_w;
          goto IDEwrite8;
        //case GIRQ_4000_OFFSET:
        //  IDE_a4k_irq = value;
          // Fallthrough
        //case GIRQ_OFFSET:
        //  IDE_irq = (IDE_irq & value) | (value & (IDE_IRQ_RESET | IDE_IRQ_BERR));
        //  return;
        //default:
        //  printf ( "%s: unserviced request 0x%x\n", __func__, ((address - IDEBASE)) );//IDE_IDE_base) - IDE_IDE_adj) );
      }
      goto skip_idewrite8;
IDEwrite8:;
//#else
      IDE_write8(atariide0, IDE_action, value);
      return;
skip_idewrite8:;
    //}

    
    return;
#endif
  }

#if (0)
  switch (address) {
    /*case 0xDD203A:
      printf("Write bye to A4000 IDE: %.2X\n", value);
      IDE_a4k = value;
      return;*/
   // case GIDENT:
      //printf("Write to GIDENT: %d\n", value);
   //   ataricounter = 0;
    //  return;
    case GCONF:
      //printf("Write to GCONF: %d\n", IDE_cfg);
      IDE_cfg = value;
      return;
    //case RAMSEY_REG:
      //ramsey_cfg = value & 0x0F;
     // return;
    case GINT:
      IDE_int = value;
      return;
    case GCS:
      IDE_cs_mask = value & ~3;
      IDE_cs &= ~3;
      IDE_cs |= value & 3;
      printf("Write to GCS: %d\n", IDE_cs);
      atariide0->selected = IDE_cs;
      return;
  }
  
#endif
  
  //if ((address & IDEMASK) == CLOCKBASE) {
  //  if ((address & CLOCKMASK) >= 0x8000) {
      //if (cdtv_mode) {
      //  //printf("[CDTV] BYTE write to SRAM @%.8X (%.8X): %.2X\n", (address & CLOCKMASK) - 0x8000, address, value);
      //  cdtv_sram[(address & CLOCKMASK) - 0x8000] = value;
      //}
   //   return;
   // }
    //printf("Byte write to RTC.\n");
    //put_rtc_byte(address, value, rtc_type);
    //DEBUG("Write Byte to IDE Space 0x%06x (0x%06x)\n", address, value);
  //  return;
  //}

  //DEBUG("Write Byte to IDE Space 0x%06x (0x%06x)\n", address, value);
}

void writeIDE(unsigned int address, unsigned int value) {
#if (1)
  if (atariide0) {
    //if (address - IDEBASE == GDATA_OFFSET) {
      IDE_write16(atariide0, IDE_data, value);
      return;
    //}

    //if (address == GIRQ_A4000) {
    //  IDE_a4k_irq = value;
    //  return;
    //}
  }

  //if ((address & IDEMASK) == CLOCKBASE) {
  //  if ((address & CLOCKMASK) >= 0x8000) {
      //if (cdtv_mode) {
      //  //printf("[CDTV] WORD write to SRAM @%.8X (%.8X): %.4X\n", (address & CLOCKMASK) - 0x8000, address, htobe16(value));
      //  ((short *) ((size_t)(cdtv_sram + (address & CLOCKMASK) - 0x8000)))[0] = htobe16(value);
      //}
  //    return;
  //  }
    //printf("Word write to RTC.\n");
    //put_rtc_byte(address + 1, (value & 0xFF), rtc_type);
    //put_rtc_byte(address, (value >> 8), rtc_type);
  //  return;
  //}
#else
    IDE_write16(atariide0, IDE_data, value);
    return;
#endif

  DEBUG("Write Word to IDE Space 0x%06x (0x%06x)\n", address, value);
}

void writeIDEL(unsigned int address, unsigned int value) {
  //if ((address & IDEMASK) == CLOCKBASE) {
    //if ((address & CLOCKMASK) >= 0x8000) {
      //if (cdtv_mode) {
      //  //printf("[CDTV] LONGWORD write to SRAM @%.8X (%.8X): %.8X\n", (address & CLOCKMASK) - 0x8000, address, htobe32(value));
      //  ((int *) (size_t)(cdtv_sram + (address & CLOCKMASK) - 0x8000))[0] = htobe32(value);
      //}
      //return;
    //}
    //printf("Longword write to RTC.\n");
    //put_rtc_byte(address + 3, (value & 0xFF), rtc_type);
    //put_rtc_byte(address + 2, ((value & 0x0000FF00) >> 8), rtc_type);
    //put_rtc_byte(address + 1, ((value & 0x00FF0000) >> 16), rtc_type);
    //put_rtc_byte(address, (value >> 24), rtc_type);
    //return;
  //}
  if (address - IDEBASE == GDATA_OFFSET) {

    //printf ("IDE write long 0x%x\n", value);

    IDE_write16(atariide0, IDE_data, value >> 16) ;
    IDE_write16(atariide0, IDE_data, value & 0xffff);
      
  }

  //DEBUG("Write Long to IDE Space 0x%06x (0x%06x)\n", address, value);
}

uint8_t readIDEB(unsigned int address) {
  if (atariide0) {
    uint8_t IDE_action = 0, IDE_val = 0;
#if (1)
    //if (address >= IDEBASE && address < (IDEBASE + IDESIZE) ) { //IDE_IDE_base && address < (IDE_IDE_base + 0x40) ) {

      //printf ("IDE read byte\n");
      switch ((address - IDEBASE) ) {//- IDE_IDE_adj) {
        case GERROR_OFFSET:
          IDE_action = IDE_error_r;
          goto ideread8;
        case GSTATUS_OFFSET:
          IDE_action = IDE_status_r;
          goto ideread8;
        case GSECTCOUNT_OFFSET:
          IDE_action = IDE_sec_count;
          goto ideread8;
        case GSECTNUM_OFFSET:
          IDE_action = IDE_sec_num;
          goto ideread8;
        case GCYLLOW_OFFSET:
          IDE_action = IDE_cyl_low;
          goto ideread8;
        case GCYLHIGH_OFFSET:
          IDE_action = IDE_cyl_hi;
          goto ideread8;
        case GDEVHEAD_OFFSET:
          IDE_action = IDE_dev_head;
          goto ideread8;
        case GCTRL_OFFSET:
          IDE_action = IDE_altst_r;
          goto ideread8;
       // case GIRQ_4000_OFFSET:
        //case GIRQ_OFFSET:
        //  return 0x80;
          //IDE_irq = (IDE_irq & value) | (value & (IDE_IRQ_RESET | IDE_IRQ_BERR));
          //default:
          //  printf ( "%s: unserviced command = 0x%x\n", __func__, ((address - IDEBASE) ));//- IDE_IDE_adj) );
      }
      goto skip_ideread8;
ideread8:;
      IDE_val = IDE_read8(atariide0, IDE_action);
      return IDE_val;
skip_ideread8:;
    //}
/*
    switch (address) {
      //case GIDENT: {
      //  uint8_t val;
      //  if (ataricounter == 0 || ataricounter == 1 || ataricounter == 3) {
      //    val = 0x80;  // 80; to enable IDE
      //  } else {
      //    val = 0x00;
      //  }
      //  ataricounter++;
        //printf("Read from GIDENT: %.2X.\n", val);
      //  return val;
      //}
      //case GINT:
      //  return IDE_int;
      //case GCONF:
        //printf("Read from GCONF: %d\n", IDE_cfg & 0x0F);
      //  return IDE_cfg & 0x0f;
      //case GCS: {
      //  uint8_t v;
      //  v = IDE_cs_mask | IDE_cs;
      //  printf("Read from GCS: %d\n", v);
      //  return v;
      //}
      // This seems incorrect, GARY_REG3 is the same as GIDENT, and the A4000
      // service manual says that Gary is accessible in the address range $DFC000 to $DFFFFF.
      case GARY_REG0:
      case GARY_REG1:
      case GARY_REG2:
        return 0;//gary_cfg[address - GARY_REG0];
        break;
      //case GARY_REG3:
      case GARY_REG4:
      //case GARY_REG5:
        return 0;//gary_cfg[address - GARY_REG4];
      case RAMSEY_ID:
        return ramsey_id;
      case RAMSEY_REG:
        return 0;//ramsey_cfg;
      case GARY_REG5: { // This makes no sense.
        uint8_t val;
        if (ataricounter == 0 || ataricounter == 1 || ataricounter == 3) {
          val = 0x80;  // 80; to enable GARY
        } else {
          val = 0x00;
        }
        ataricounter++;
        return val;
      }
      //case 0xDD203A:
        // This can't be correct, as this is the same address as GDEVHEAD on the A4000 IDE.
        //printf("Read Byte from IDE A4k: %.2X\n", IDE_a4k);
        //return IDE_a4k;

      default:
        printf ( "%s: unknown command 0x%x\n", __func__, address );
    }
    */
  }

  //if ((address & IDEMASK) == CLOCKBASE) {
  //  if ((address & CLOCKMASK) >= 0x8000) {
      //if (cdtv_mode) {
      //  //printf("[CDTV] BYTE read from SRAM @%.8X (%.8X): %.2X\n", (address & CLOCKMASK) - 0x8000, address, cdtv_sram[(address & CLOCKMASK) - 0x8000]);
      //  return cdtv_sram[(address & CLOCKMASK) - 0x8000];
      //}
  //    return 0;
  //  }
    //printf("Byte read from RTC.\n");
  //  return 0xff; //get_rtc_byte(address, rtc_type);
  //}
#else
    DEBUG("Read Byte From IDE Space 0x%06x\n", address);
    IDE_val = IDE_read8(atariide0, IDE_action);
    return IDE_val;
  }
#endif
  //DEBUG("Read Byte From IDE Space 0x%06x\n", address);
  return 0xFF;
}

uint16_t readIDE(unsigned int address) {
  uint16_t value;
  if (atariide0) {
#if (1)
    if (address - IDEBASE == GDATA_OFFSET) {
      
      //printf ("IDE read word\n");

      uint16_t value;
      value = IDE_read16(atariide0, IDE_data);
      //	value = (value << 8) | (value >> 8);
      return value;//value << 8 | value >> 8;
    }

    //if (address == GIRQ_A4000) {
    //  IDE_a4k_irq = 0x8000;
    //  return 0x8000;
    //}
  }

  //if ((address & IDEMASK) == CLOCKBASE) {
  //  if ((address & CLOCKMASK) >= 0x8000) {
      //if (cdtv_mode) {
      //  //printf("[CDTV] WORD read from SRAM @%.8X (%.8X): %.4X\n", (address & CLOCKMASK) - 0x8000, address, be16toh( (( unsigned short *) (size_t)(cdtv_sram + (address & CLOCKMASK) - 0x8000))[0]));
      //  return be16toh( (( unsigned short *) (size_t)(cdtv_sram + (address & CLOCKMASK) - 0x8000))[0]);
      //}
  //    return 0;
  //  }
    //printf("Word read from RTC.\n");
  //  return 0xffff; //((get_rtc_byte(address, rtc_type) << 8) | (get_rtc_byte(address + 1, rtc_type)));
  //}
#else
    value = IDE_read16(atariide0, IDE_data);
    DEBUG("Read Word From IDE Space 0x%06x\n", address);
    return value;
  }
#endif
  //DEBUG("Read Word From IDE Space 0x%06x\n", address);
  return 0x8000;
}

uint32_t readIDEL(unsigned int address) {
  //if ((address & IDEMASK) == CLOCKBASE) {
    //if ((address & CLOCKMASK) >= 0x8000) {
      //if (cdtv_mode) {
      //  //printf("[CDTV] LONGWORD read from SRAM @%.8X (%.8X): %.8X\n", (address & CLOCKMASK) - 0x8000, address, be32toh( (( unsigned short *) (size_t)(cdtv_sram + (address & CLOCKMASK) - 0x8000))[0]));
      //  return be32toh( (( unsigned int *) (size_t)(cdtv_sram + (address & CLOCKMASK) - 0x8000))[0]);
      //}
     // return 0;
    //}
    //printf("Longword read from RTC.\n");
    //return 0xffffffff; //((get_rtc_byte(address, rtc_type) << 24) | (get_rtc_byte(address + 1, rtc_type) << 16) | (get_rtc_byte(address + 2, rtc_type) << 8) | (get_rtc_byte(address + 3, rtc_type)));
  //}
  if (address - IDEBASE == GDATA_OFFSET) {

   // printf ("IDE read long\n");

      uint32_t value;

      value = IDE_read16(atariide0, IDE_data);
      
      return value << 16 | IDE_read16(atariide0, IDE_data) ;
    }
  //DEBUG("Read Long From IDE Space 0x%06x\n", address);
  return 0x8000;
}
