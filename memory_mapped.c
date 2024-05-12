// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"
#include "m68k.h"
#include <endian.h>
#include <stdbool.h>
#include "platforms/atari/et4000.h"

//#define CHKRANGE(a, b, c) a >= (unsigned int)b && a < (unsigned int)(b + c)
#define CHKRANGE_ABS(a, b, c) a >= (uint32_t)b && a < (uint32_t) c

static uint32_t target;
extern bool ET4000Initialised;
//extern volatile int g_irq;
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


inline int handle_mapped_read ( struct emulator_config *cfg, uint32_t addr, uint32_t *val, unsigned char type ) 
{
  uint8_t *read_addr = NULL;

  for ( int i = 0; i < MAX_NUM_MAPPED_ITEMS && cfg->map_type [i] != MAPTYPE_NONE; i++ ) 
  {
    if ( CHKRANGE_ABS ( addr, cfg->map_offset [i], cfg->map_high [i] ) )
    {
      switch ( cfg->map_type [i] ) 
      {
        case MAPTYPE_ROM:
        case MAPTYPE_RAM:
        case MAPTYPE_RAM_WTC:
        case MAPTYPE_RAM_NOALLOC:
        case MAPTYPE_FILE:
        
          //if ( ET4000Initialised && (addr >= NOVA_ET4000_VRAMBASE && addr < 0x00DC0400) )
          //{
          //  et4000Read ( addr, &target, type );

          //  *val = target;

          //  return 1;
          //}

          //else
          //{
            read_addr = cfg->map_data [i] + ( addr - cfg->map_offset [i] );
          //}
         
          break;

        case MAPTYPE_REGISTER:

          if ( cfg->platform->register_read ( addr, type, &target ) != -1 ) 
          {
            *val = target;
      
            return 1;
          }
          
          return -1;
          break;

        default:
          return -1;
          break;
      }
    }
  }

  if ( read_addr == NULL )
    return -1;
    
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

  if ( type == OP_TYPE_WORD )
  {
    *val = be16toh ( *( (uint16_t *)read_addr ) );
    return 1;
  }

  if ( type == OP_TYPE_LONGWORD )
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

  for ( int i = 0; i < MAX_NUM_MAPPED_ITEMS && cfg->map_type[i] != MAPTYPE_NONE; i++ ) 
  {
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
    /* need to determine what type of memory access this is
       only way to do this is to look at address */
    if ( CHKRANGE_ABS ( addr, cfg->map_offset[i], cfg->map_high[i] ) ) 
    {
      switch ( cfg->map_type [i] ) 
      {
        case MAPTYPE_ROM:

          return 1;
          break;

        case MAPTYPE_RAM:
        //case MAPTYPE_RAM_NOALLOC:
        //case MAPTYPE_FILE:

          write_addr = cfg->map_data [i] + ( addr - cfg->map_offset [i] );
          res = 1;

          goto write_value;
          break;

        case MAPTYPE_RAM_WTC:
          
          write_addr = cfg->map_data [i] + ( addr - cfg->map_offset [i] );
          res = -1;

          goto write_value;
          break;

        case MAPTYPE_REGISTER:
  
          return cfg->platform->register_write ( addr, value, type );
          break;
      }
    }
  }

  return res;

write_value:
  switch ( type ) 
  {
    case OP_TYPE_BYTE:

      *write_addr = (uint8_t)value;
      
      break;

    case OP_TYPE_WORD:

      *(uint16_t *)write_addr = htobe16 ( value );

      break;

    case OP_TYPE_LONGWORD:

      *(uint32_t *)write_addr = htobe32 ( value );

      break;

    case OP_TYPE_MEM:
    
      res = -1;

      break;
  }

  return res;
}
