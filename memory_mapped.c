// SPDX-License-Identifier: MIT

#include "config_file/config_file.h"
#include "m68k.h"
#include <endian.h>
#include <stdbool.h>
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


inline int handle_mapped_read ( struct emulator_config *cfg, uint32_t addr, uint32_t *val, unsigned char type ) 
{
  uint8_t *read_addr = NULL;

  for ( int i = 0; i < MAX_NUM_MAPPED_ITEMS && cfg->map_type [i] != MAPTYPE_NONE; i++ ) 
  {
    if ( CHKRANGE_ABS ( addr, cfg->map_offset [i], cfg->map_high [i] ) )
    //if ( addr >= cfg->map_offset [i] && addr < cfg->map_high [i] )
    {
      switch ( cfg->map_type [i] ) 
      {
        case MAPTYPE_ROM:
        case MAPTYPE_RAM:
        case MAPTYPE_RAM_WTC:
        case MAPTYPE_RAM_NOALLOC:
        case MAPTYPE_FILE:
        
          //if ( addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_VRAMTOP )
          //{        
           // read_addr = RTGbuffer + (addr - NOVA_ET4000_VRAMBASE);
          //}

          //else
            read_addr = cfg->map_data [i] + ( addr - cfg->map_offset [i] );
         
          break;
        case MAPTYPE_REGISTER:
          //if ( cfg->platform && cfg->platform->register_read ) 
          //{
          if ( cfg->platform->register_read ( addr, type, &target ) != -1 ) 
          {
            *val = target;
            //printf ( "*val = 0x%X\n", *val );
            return 1;
          }
          //}
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
#if (1)
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
  //for (int i = 0; i < MAX_NUM_MAPPED_ITEMS; i++) 
  {
    //if (cfg->map_type[i] == MAPTYPE_NONE)
    //  break;
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
#ifdef RAYLIB
          if ( RTG_enabled && RTG_VRAM_BASE && ( addr >= RTG_VRAM_BASE && addr < RTG_VRAM_BASE | RTG_VRAM_SIZE ) )
          {
            vramLock = 1;
          }
#endif
          //if ( addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_VRAMTOP )
          //{
           // write_addr = RTGbuffer + (addr - NOVA_ET4000_VRAMBASE);
          //}

           // printf ( "mapped write 0x%X\n", addr );
          //else
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
          //if ( cfg->platform && cfg->platform->register_write ) 
          //{
            return cfg->platform->register_write ( addr, value, type );
          //}
          break;
      }
    }
  }

  return res;

write_value:
  switch ( type ) 
  {
    case OP_TYPE_BYTE:
      write_addr[0] = (unsigned char)value;
      
#ifndef RAYLIB
      //return res;
#endif
      break;
    case OP_TYPE_WORD:
      ((uint16_t *)write_addr)[0] = htobe16 ( value );
#ifndef RAYLIB
      //return res;
#endif
      break;
    case OP_TYPE_LONGWORD:
      ((uint32_t *)write_addr)[0] = htobe32 ( value );
#ifndef RAYLIB
      //return res;
#endif
      break;
    case OP_TYPE_MEM:
      //RTG_VSYNC = 0;
      //return -1;
      res = -1;
      break;
  }

#ifdef RAYLIB
  vramLock = 0;
#endif

  //RTG_VSYNC = 0;
  return res;
}
