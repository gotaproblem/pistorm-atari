// SPDX-License-Identifier: MIT

//
//  IDE.c
//  Originally based on Omega's IDE emulation,
//  created by Matt Parsons on 06/03/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//

#define _LARGEFILE64_SOURCE 

#include "platforms/atari/IDE.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <endian.h>
#include "config_file/config_file.h"
#include "atari-registers.h"
#include "platforms/atari/idedriver.h"

#define DEBUGPRINT 0
#if DEBUGPRINT
#define DEBUG_PRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...) ;
#endif

#define DEBUG_IDE
#ifdef DEBUG_IDE
#define DEBUG ; // printf
#else
#define DEBUG(...)
#endif

//#define IDE_DUMMY 
//int IDEIF;

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
  DEBUG_PRINTF ("[!!!IDE] No IDE emulation layer available, HDF image not attached.\n");
  return;
}

void IDE_attach(uint8_t *dummy, uint32_t idx, uint32_t atarifd) {
  if (dummy || idx || atarifd) {};
  DEBUG_PRINTF ("[!!!IDE] No IDE emulation layer available, image not mounted.\n");
  return;
}
#else
static struct ide_controller *atariIDE [4] = {NULL, NULL, NULL, NULL};
#endif

int atarifd;
char *atari_image_file[IDE_MAX_HARDFILES];
uint8_t IDE_IDE_enabled;
//uint8_t IDE_emulation_enabled = 1;

struct ide_controller *get_ide ( int index ) 
{
  //if ( index ) {}

  return atariIDE [index];
}


void set_hard_drive_image_file_atari ( uint8_t index, char *filename ) 
{
  if (atari_image_file[index] != NULL)
    free(atari_image_file[index]);

  atari_image_file[index] = calloc(1, strlen(filename) + 1);
  strcpy(atari_image_file[index], filename);
}

void InitIDE (void) 
{
  uint8_t num_IDE_drives = 0;
  int port = 0;
  int ix;

  //get_mapped_item_by_address ( cfg, 0x00F00000 );
  ix = (0x00 & 0xf0) >> 6; /* get IDE interface number 0 - 3 */



 // for ( int n = 0; n < 4; n++ )
  //{
    //for (int i = 0; i < 2; i++ )//IDE_MAX_HARDFILES; i++) 
    for ( int i = 0; i < IDE_MAX_HARDFILES && port < 4; i++ ) 
    {
      //printf ( "here 1\n" );
      port = (i / 2); //+ ix;
      //printf ( "port %d\n", port );

      if ( atari_image_file [i] ) 
      {
       // printf ( "here 2\n" );
        atarifd = open ( atari_image_file[i], O_RDWR | O_LARGEFILE );

        if (atarifd != -1) 
        {
         // printf ( "here 3\n" );
          if ( ! atariIDE [port] )
              atariIDE [port] = IDE_allocate ( "cf" );
        }

        if (atarifd == -1) 
        {
          printf ( "[IDE%d:] [HDD%d] HDD Image %s failed to open\n", port, i, atari_image_file[i] );
        } 
        
        else 
        {
         // printf ( "here 4\n" );
          if ( strcmp ( atari_image_file [i] + ( strlen (atari_image_file [i] ) - 2 ), "ST" ) == 0 )
          {
            printf ( "[FDD%d] Attaching FDD image %s.\n", i, atari_image_file [i] );

            ide_attach_st ( atariIDE [port], i & 1, atarifd );
            num_IDE_drives++;

            printf ( "[FDD%d] FDD Image %s attached\n", i, atari_image_file [i] );
          }

          else if ( strcmp ( atari_image_file [i] + ( strlen (atari_image_file [i] ) - 3 ), "img" ) == 0 
          || strncmp ( atari_image_file [i], "/dev/loop0", 10 ) == 0 )
          {
            //printf ("[HDD%d] Attaching HDD image %s.\n", i, atari_image_file[i]);
            //if (strcmp(atari_image_file[i] + (strlen(atari_image_file[i]) - 3), "img") != 0) {
              //DEBUG_PRINTF ("No header present on HDD image %s.\n", atari_image_file[i]);
              ide_attach_hdf ( atariIDE [port], i & 1, atarifd );
              num_IDE_drives++;
            //}
            //else {
            //  DEBUG_PRINTF ("Attaching HDD image with header.\n");
            //  IDE_attach(atariide0, i, atarifd);
            //  num_IDE_drives++;
            //}
            printf ("[IDE%d:] [HDD%d] HDD Image %s attached\n", port, i, atari_image_file[i]);
          }
        }
      }

      
    }
  
    for ( int n = 0; n < 4; n ++ )
      if ( atariIDE [n] )
        IDE_reset_begin ( atariIDE [n] );
  //}


  if (num_IDE_drives == 0) 
  {
    // No IDE drives mounted, disable IDE component of IDE
    //DEBUG_PRINTF ("No IDE drives mounted, disabling IDE component.\n");
    IDE_IDE_enabled = 0;
  }

  else
    IDE_IDE_enabled = 1;
}

static uint8_t IDE_action = 0;

void writeIDEB ( uint32_t address, unsigned int value ) 
{
  static int port;
  static int base;

  port = (address & 0xf0) >> 6; /* get IDE interface number 0 - 3 */

  if ( atariIDE [port] ) 
  {
    base = address - ( IDEBASE + (0x40 * port) );

    switch ( base ) 
    {
      case GFEAT_OFFSET:
        //DEBUG_PRINTF ("Write to GFEAT: %.2X.\n", value);
        IDE_action = IDE_feature_w;
       // goto IDEwrite8;
        break;
      case GCMD_OFFSET:
        //DEBUG_PRINTF ("Write to GCMD: %.2X.\n", value);
        IDE_action = IDE_command_w;
        //goto IDEwrite8;
        break;
      case GSECTCOUNT_OFFSET:
        IDE_action = IDE_sec_count;
        //goto IDEwrite8;
        break;
      case GSECTNUM_OFFSET:
        IDE_action = IDE_sec_num;
        //goto IDEwrite8;
        break;
      case GCYLLOW_OFFSET:
        IDE_action = IDE_cyl_low;
        //goto IDEwrite8;
        break;
      case GCYLHIGH_OFFSET:
        IDE_action = IDE_cyl_hi;
        //goto IDEwrite8;
        break;
      case GDEVHEAD_OFFSET:
        //DEBUG_PRINTF ("Write to GDEVHEAD: %.2X.\n", value);
        IDE_action = IDE_dev_head;
        //goto IDEwrite8;
        break;
      case GCTRL_OFFSET:
        //DEBUG_PRINTF ("Write to GCTRL: %.2X.\n", value);
        IDE_action = IDE_devctrl_w;
       // goto IDEwrite8;
       break;
      //case GIRQ_4000_OFFSET:
      //  IDE_a4k_irq = value;
        // Fallthrough
      //case GIRQ_OFFSET:
      //  IDE_irq = (IDE_irq & value) | (value & (IDE_IRQ_RESET | IDE_IRQ_BERR));
      //  return;
      default:
      //  printf ( "%s: unserviced request 0x%x\n", __func__, ((address - IDEBASE)) );//IDE_IDE_base) - IDE_IDE_adj) );
        return;
    }

    //goto skip_idewrite8;

IDEwrite8:
    IDE_write8 ( atariIDE [port], IDE_action, value );

    return;

//skip_idewrite8:;
//    return;
  }

#if (0)
  switch (address) {
    /*case 0xDD203A:
      DEBUG_PRINTF ("Write bye to A4000 IDE: %.2X\n", value);
      IDE_a4k = value;
      return;*/
   // case GIDENT:
      //DEBUG_PRINTF ("Write to GIDENT: %d\n", value);
   //   ataricounter = 0;
    //  return;
    case GCONF:
      //DEBUG_PRINTF ("Write to GCONF: %d\n", IDE_cfg);
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
      DEBUG_PRINTF ("Write to GCS: %d\n", IDE_cs);
      atariide0->selected = IDE_cs;
      return;
  }
  
#endif

  //DEBUG("Write Byte to IDE Space 0x%06x (0x%06x)\n", address, value);
}

void writeIDE ( uint32_t address, unsigned int value) 
{
  static int port;
  static int base;

  port = (address & 0xf0) >> 6; /* get IDE interface number 0 - 3 */
  base = address - ( IDEBASE + (0x40 * port) );

  if ( atariIDE [port] ) 
  {
    if ( base == GDATA_OFFSET )
      IDE_write16 ( atariIDE [port], IDE_data, value );

    return;
  }

  //DEBUG("Write Word to IDE Space 0x%06x (0x%06x)\n", address, value);
}

void writeIDEL ( uint32_t address, unsigned int value ) 
{
  static int port;
  static int base;

  port = (address & 0xf0) >> 6; /* get IDE interface number 0 - 3 */
  base = address - ( IDEBASE + (0x40 * port) );

  if ( atariIDE [port] ) 
  {
    if ( base == GDATA_OFFSET )
    {
      IDE_write16 ( atariIDE [port], IDE_data, value >> 16 ) ;
      IDE_write16 ( atariIDE [port], IDE_data, value & 0xffff );
    }
  }
  //DEBUG("Write Long to IDE Space 0x%06x (0x%06x)\n", address, value);
}

uint8_t readIDEB ( uint32_t address ) 
{
  static int port;
  static int base;
  static uint8_t IDE_action;

  port = (address & 0xf0) >> 6; /* get IDE interface number 0 - 3 */
  base = address - ( IDEBASE + (0x40 * port) );

  if ( atariIDE [port] ) 
  {
    //uint8_t IDE_action = 0, IDE_val = 0;

      switch ( base ) 
      {
        case GERROR_OFFSET:
          IDE_action = IDE_error_r;
          break; //goto ideread8;
        case GSTATUS_OFFSET:
          IDE_action = IDE_status_r;
          break; //goto ideread8;
        case GSECTCOUNT_OFFSET:
          IDE_action = IDE_sec_count;
          break; //goto ideread8;
        case GSECTNUM_OFFSET:
          IDE_action = IDE_sec_num;
          break; //goto ideread8;
        case GCYLLOW_OFFSET:
          IDE_action = IDE_cyl_low;
          break; //goto ideread8;
        case GCYLHIGH_OFFSET:
          IDE_action = IDE_cyl_hi;
          break; //goto ideread8;
        case GDEVHEAD_OFFSET:
          IDE_action = IDE_dev_head;
          break; //goto ideread8;
        case GCTRL_OFFSET:
          IDE_action = IDE_altst_r;
          break; //goto ideread8;

       // case GIRQ_4000_OFFSET:
        //case GIRQ_OFFSET:
        //  return 0x80;
          //IDE_irq = (IDE_irq & value) | (value & (IDE_IRQ_RESET | IDE_IRQ_BERR));
          //default:
          //  printf ( "%s: unserviced command = 0x%x\n", __func__, ((address - IDEBASE) ));//- IDE_IDE_adj) );
        default:
          return 0xFF;
      }
      
      return IDE_read8 ( atariIDE [port], IDE_action );
  }

  //DEBUG("Read Byte From IDE Space 0x%06x\n", address);
  return 0xFF;
}


uint16_t readIDE ( uint32_t address ) 
{
  static int port;
  static int base;

  port = (address & 0xf0) >> 6; /* get IDE interface number 0 - 3 */
  base = address - ( IDEBASE + (0x40 * port) );

  if ( atariIDE [port] ) 
  {
    if ( base == GDATA_OFFSET ) 
    {
      return IDE_read16 ( atariIDE [port], IDE_data );
    }

    //if (address == GIRQ_A4000) {
    //  IDE_a4k_irq = 0x8000;
    //  return 0x8000;
    //}
  }

  //DEBUG("Read Word From IDE Space 0x%06x\n", address);
  return 0x8000;
}

uint32_t readIDEL ( uint32_t address ) 
{
  static int port;
  static int base;
  static uint32_t value;
  
  port = (address & 0xf0) >> 6; /* get IDE interface number 0 - 3 */
  base = address - ( IDEBASE + (0x40 * port) );

  if ( atariIDE [port] ) 
  {
    if ( base == GDATA_OFFSET ) 
    {
      value = IDE_read16 ( atariIDE [port], IDE_data );
      
      return value << 16 | IDE_read16 ( atariIDE [port], IDE_data ) ;
    }
  }

  //DEBUG("Read Long From IDE Space 0x%06x\n", address);
  return 0x8000;
}
