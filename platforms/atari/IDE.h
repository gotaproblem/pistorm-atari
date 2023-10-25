//SPDX-License-Identifier: MIT

//
//  IDE.h
//  Omega
//
//  Created by Matt Parsons on 06/03/2019.
//  Copyright © 2019 Matt Parsons. All rights reserved.
//

#ifndef IDE_h
#define IDE_h

#define IDE_MAX_HARDFILES 8

#include <stdio.h>
#include <stdint.h>

void InitIDE(void);
void writeIDEB(unsigned int address, unsigned value);
void writeIDE(unsigned int address, unsigned value);
void writeIDEL(unsigned int address, unsigned value);
uint8_t readIDEB(unsigned int address);
uint16_t readIDE(unsigned int address);
uint32_t readIDEL(unsigned int address);

struct ide_controller *get_ide(int index);

// IDE IDE read offsets
#define GERROR_OFFSET 0x05
#define GSTATUS_OFFSET 0x1d //0x1f //0x39
// IDE IDE write offsets
#define GFEAT_OFFSET 0x05
#define GCMD_OFFSET 0x1d //0x1f // might be 0x39 also
// IDE IDE RW offsets
#define GDATA_OFFSET 0x00
#define GSECTCOUNT_OFFSET 0x09
#define GSECTNUM_OFFSET 0x0d
#define GCYLLOW_OFFSET 0x11
#define GCYLHIGH_OFFSET 0x15
#define GDEVHEAD_OFFSET 0x19
#define GCTRL_OFFSET 0x39 //0x1018
//#define GIRQ_OFFSET 0x7000
//#define GIRQ_4000_OFFSET 0x1002

// IDE IDE Writes
#define GFEAT    0xf00004 //0xda2004  // Write : Feature
#define GCMD     0xf0001e //0xda201c   // Write : Command

// IDE IDE RW
#define GDATA    0xf00000 //0xda2000     // Data - 16 bit
#define GSECTCNT 0xf00008 //0xda2008  // SectorCount
#define GSECTNUM 0xf0000c //0xda200c  // SectorNumber
#define GCYLLOW  0xf00010 //0xda2010   // CylinderLow
#define GCYLHIGH 0xf00014 //0xda2014  // CylinderHigh
#define GDEVHEAD 0xf00018 //0xda2018  // Device/Head
#define GCTRL    0xf00038 //0xda3018     // Control

// IDE Ident
//#define GIDENT 0xDE1000

// IDE IRQ/CC
//#define GCS 0xDA8000   // Card Control
//#define GIRQ 0xDA9000  // IRQ
//#define GINT 0xDAA000  // Int enable
//#define GCONF 0xDAB000  // IDE Config

#endif /* IDE_h */
