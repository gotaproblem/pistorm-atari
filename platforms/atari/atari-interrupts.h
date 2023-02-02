// SPDX-License-Identifier: MIT

#ifndef PISTORM_ATARI_INTERRUPTS_H
#define PISTORM_ATARI_INTERRUPTS_H

typedef enum {
  TBE,    //Serial port transmit buffer empty
  DSKBLK, //Disk block finished
  SOFT,   //Reserved for software initiated interrupt
  PORTS,  //I/O Ports and timers
  COPER,  //Coprocessor
  VERTB,  //Start of vertical blank
  BLIT,   //Blitter has finished
  AUD0,   //Audio channel 0 block finished
  AUD1,   //Audio channel 1 block finished
  AUD2,   //Audio channel 2 block finished
  AUD3,   //Audio channel 3 block finished
  RBF,    //Serial port receive buffer full
  DSKSYN, //Disk sync register (DSKSYNC) matches disk
  EXTER,  //External interrupt
} ATARI_IRQ;

void atari_emulate_irq(ATARI_IRQ irq);
uint8_t get_atari_emulated_ipl();
int atari_emulating_irq(ATARI_IRQ irq);
void atari_clear_emulating_irq();
int atari_handle_intrqr_read(uint32_t *res);
int atari_handle_intrq_write(uint32_t val);

#endif //PISTORM_ATARI_INTERRUPTS_H
