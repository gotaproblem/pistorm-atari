// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"
#include "m68k.h"
#include <endian.h>
#include <stdbool.h>
#include <stdio.h>
#include "platforms/atari/et4000.h"

//#define CHKRANGE(a, b, c) a >= (unsigned int)b && a < (unsigned int)(b + c)
#define CHKRANGE_ABS(a, b, c) a >= (uint32_t)b && a < (uint32_t) c

static unsigned int target;
//extern int ovl;
extern volatile int g_irq;
//extern const char *map_type_names[MAPTYPE_NUM];
//const char *op_type_names[OP_TYPE_NUM] = {
//  "BYTE",
//  "WORD",
//  "LONGWORD",
//  "MEM",
//};

//extern uint8_t IDE_IDE_enabled;

#ifdef RAYLIB
extern int RTG_enabled;
extern volatile uint32_t RTG_VRAM_BASE;
extern volatile uint32_t RTG_VRAM_SIZE;
extern volatile int vramLock;
#endif

extern volatile uint32_t RTG_VSYNC;
extern void *RTGbuffer;


/*
 * return -1 if an ATARI address to read
 * return 1 if a successful mapped read
 */
inline 
int handle_mapped_read ( struct emulator_config *cfg, uint32_t addr, uint32_t *val, unsigned char type ) 
{
  uint8_t *read_addr = NULL;

  //for ( int i = 0; i < MAX_NUM_MAPPED_ITEMS && cfg->map_type [i] != MAPTYPE_NONE; i++ ) 
  for ( int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++ ) 
  {
    if ( cfg->map_type [i] == MAPTYPE_NONE )
      break;
  
    if ( CHKRANGE_ABS ( addr, cfg->map_offset [i], cfg->map_high [i] ) )
    {
      switch ( cfg->map_type [i] ) 
      {
        case MAPTYPE_RAM:
          //printf ( "%s checking RAM - type %d, 0x%X, 0x%X\n", __func__, cfg->map_type [i], cfg->map_offset [i], cfg->map_high [i] );
          read_addr = cfg->map_data [i] + ( addr - cfg->map_offset [i] );
          break;

        case MAPTYPE_ROM:
          //printf ( "%s checking ROM - type %d, 0x%X, 0x%X\n", __func__, cfg->map_type [i], cfg->map_offset [i], cfg->map_high [i] );
          read_addr = cfg->map_data [i] + ( ( addr - cfg->map_offset [i] ) % cfg->rom_size[i] );
          break;

        case MAPTYPE_RAM_WTC:
        case MAPTYPE_RAM_NOALLOC:
        case MAPTYPE_FILE:
        
          read_addr = cfg->map_data [i] + ( addr - cfg->map_offset [i] );
          break;

        case MAPTYPE_REGISTER:

          if ( cfg->platform && cfg->platform->register_read ) 
          {
            if ( cfg->platform->register_read ( addr, type, &target ) != -1 ) 
            {
              *val = target;
              //printf ( "*val = 0x%X\n", *val );
              return 1;
            }
          }

          return -1;
          break;

        default:

          return -1;
          break;
      }
    }

   // else
    //  printf ( "%s %p out of expected range 0x%X - 0x%X\n", __func__, addr, cfg->map_offset [i], cfg->map_high [i] );
  }

  /* if NULL then addr is not mapped - that is, it is an ATARI address */
  if ( read_addr == NULL )
  {
    //printf ( "%s NULL read addres %p\n", __func__, addr );
    return -1;
  }
#if (0)
  switch ( type ) 
  {
    case OP_TYPE_BYTE:
      *val = read_addr [0];
      //return 1;
      break;
    case OP_TYPE_WORD:
      *val = be16toh ( ( (uint16_t *)read_addr ) [0] );
      //return 1;
      break;
    case OP_TYPE_LONGWORD:
      *val = be32toh ( ( (uint32_t *)read_addr ) [0] );
      //return 1;
      break;
    case OP_TYPE_MEM:
      //RTG_VSYNC = 0;
      return -1;
      break;
  }

  //RTG_VSYNC = 0;
  return 1;
#else
  if ( type == OP_TYPE_BYTE )
  {
    *val = *read_addr;
    return 1;
  }

  else if ( type == OP_TYPE_WORD )
  {
    *val = be16toh ( *( (uint16_t *)read_addr ) );
    return 1;
  }

  else if ( type == OP_TYPE_LONGWORD )
  {
    *val = be32toh ( *( (uint32_t *)read_addr ) );
    return 1;
  }

  return -1;
#endif
}




inline int handle_mapped_write ( struct emulator_config *cfg, uint32_t addr, uint32_t value, unsigned char type ) 
{
  int res = -1;
  uint8_t *write_addr = NULL;

  //for ( int i = 0; i < MAX_NUM_MAPPED_ITEMS && cfg->map_type[i] != MAPTYPE_NONE; i++ ) 
  for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) 
  {
    if (cfg->map_type[i] == MAPTYPE_NONE)
      break;
#if (0)    
    //else if (ovl && cfg->map_type[i] == MAPTYPE_RAM_WTC) {
    if ( cfg->map_type[i] == MAPTYPE_RAM_WTC) 
    {
      if (cfg->map_mirror[i] != ((unsigned int)-1) && CHKRANGE(addr, cfg->map_mirror[i], cfg->map_size[i])) 
      {
        write_addr = cfg->map_data[i] + ((addr - cfg->map_mirror[i]) % cfg->rom_size[i]);
        res = -1;
        goto write_value;
      }
    }
#endif
    if ( CHKRANGE_ABS ( addr, cfg->map_offset[i], cfg->map_high[i] ) ) 
    {
      switch ( cfg->map_type [i] ) 
      {
        case MAPTYPE_ROM:

          return 1;
          break;

        case MAPTYPE_RAM:
        case MAPTYPE_RAM_NOALLOC:
        case MAPTYPE_FILE:

          write_addr = cfg->map_data [i] + ( addr - cfg->map_offset [i] );

          res = 1;
          
          goto write_value;
          break;

        case MAPTYPE_RAM_WTC:

          //printf("Some write to WTC RAM.\n");
          write_addr = cfg->map_data [i] + ( addr - cfg->map_offset [i] );
          res = -1;
          goto write_value;
          break;

        case MAPTYPE_REGISTER:

          //printf ( "register write: addr 0x%X, value 0x%X/n", addr, value );
          if ( cfg->platform && cfg->platform->register_write ) 
          {
            return cfg->platform->register_write ( addr, value, type );
          }
          break;
      }
    }
  }

  return res;

write_value:

  if ( type == OP_TYPE_BYTE )
    write_addr [0] = (unsigned char)value;

  else if ( type == OP_TYPE_WORD )
    ((uint16_t *)write_addr) [0] = htobe16 ( value );

  else if ( type == OP_TYPE_LONGWORD )
    ((uint32_t *)write_addr) [0] = htobe32 ( value );

  else
    res = -1;
/*
  switch ( type ) 
  {
    case OP_TYPE_BYTE:

      write_addr[0] = (unsigned char)value;
      
      break;

    case OP_TYPE_WORD:

      ((uint16_t *)write_addr)[0] = htobe16 ( value );

      break;

    case OP_TYPE_LONGWORD:

      ((uint32_t *)write_addr)[0] = htobe32 ( value );

      break;

    case OP_TYPE_MEM:
    
      res = -1;

      break;
  }
*/
  return res;
}
