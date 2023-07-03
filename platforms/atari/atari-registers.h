
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

#define BLITTERBASE 0x00ff8a00
#define BLITTERSIZE 0x3e


