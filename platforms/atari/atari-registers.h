
// SPDX-License-Identifier: MIT

void configure_rtc_emulation_atari(uint8_t enabled);
void set_hard_drive_image_file_atari(uint8_t index, char *filename);
int custom_read_atari(struct emulator_config *cfg, unsigned int addr, unsigned int *val, unsigned char type);
int custom_write_atari(struct emulator_config *cfg, unsigned int addr, unsigned int val, unsigned char type);
int handle_register_read_atari(unsigned int addr, unsigned char type, unsigned int *val);
int handle_register_write_atari(unsigned int addr, unsigned int value, unsigned char type);

void adjust_IDE_4000();
void adjust_IDE_1200();

#define IDEBASE 0xf00000 //0xD80000
#define IDESIZE 0x40 //0x070000
//#define IDEMASK 0xd800ff //0xDF0000

#define CLOCKBASE 0xDC0000
#define CLOCKSIZE 0x010000
#define CLOCKMASK 0x00FFFF

/* GARY ADDRESSES */
#define GARY_REG0 0xDE0000
#define GARY_REG1 0xDE0001
#define GARY_REG2 0xDE0002
#define GARY_REG3 0xDE1000
#define GARY_REG4 0xDE1001
#define GARY_REG5 0xDE1002

#define INTENAR 0xDFF01C
#define INTREQR 0xDFF01E
#define INTENA 0xDFF09A
#define INTREQ 0xDFF09C

#define JOY0DAT 0xDFF00A
#define JOY1DAT 0xDFF00C
#define CIAAPRA 0xBFE001
#define CIAADAT 0xBFEC01
#define CIAAICR 0xBFED01
#define CIAACRA 0xBFEE01
#define CIAACRB 0xBFEF01

#define CIABPRA 0xBFD000
#define CIABPRB 0xBFD100

#define POTGOR  0xDFF016
#define SERDAT  0xDFF030

#define VPOSR   0xDFF004
#define VPOSW   0xDFF02A
#define DMACON  0xDFF096
#define DMACONR 0xDFF002

#define SEL0_BITNUM 3

/* RAMSEY ADDRESSES */
#define RAMSEY_REG 0xDE0003 /* just a nibble, it should return 0x08 for defaults with 16MB */
#define RAMSEY_ID 0xDE0043  /* Either 0x0D or 0x0F (most recent version) */
/* RAMSEY TYPES */
#define RAMSEY_REV4 0x0D
#define RAMSEY_REV7 0x0F
