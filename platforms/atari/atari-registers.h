
// SPDX-License-Identifier: MIT

void configure_rtc_emulation_atari(uint8_t enabled);
void set_hard_drive_image_file_atari(uint8_t index, char *filename);
int custom_read_atari(struct emulator_config *cfg, unsigned int addr, unsigned int *val, unsigned char type);
int custom_write_atari(struct emulator_config *cfg, unsigned int addr, unsigned int val, unsigned char type);
int handle_register_read_atari ( uint32_t addr, unsigned char type, unsigned int *val);
int handle_register_write_atari ( uint32_t addr, unsigned int value, unsigned char type);

void adjust_IDE_4000();
void adjust_IDE_1200();

#define IDEBASE 0x00f00000
#define IDESIZE 0x40
#define IDETOP  (IDEBASE | IDESIZE)

#define BLITTERBASE 0x00ff8a00
#define BLITTERSIZE 0x3e


