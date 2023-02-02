// SPDX-License-Identifier: MIT

#include <stdint.h>
#include "config_file/config_file.h"
#include "atari-registers.h"
#include "atari-interrupts.h"
#include "gpio/ps_protocol.h"

//static const uint8_t IPL[14] = {1, 1, 1, 2, 3, 3, 3, 4, 4, 4, 4, 5, 5, 6 };
/* cryptodad ATARI table */
/* IPL (hardware priority) number to pseudo interrupt level */
static const uint8_t IPL[14] = {6,6,6,6,6,6,6,6,6,6,6,6,6,6 };

uint16_t atari_emulated_irqs = 0x0000;
uint8_t atari_emulated_ipl;

void atari_emulate_irq(ATARI_IRQ irq) {
  atari_emulated_irqs |= 1 << irq;
  uint8_t ipl = IPL[irq];

  if (atari_emulated_ipl < ipl) {
    atari_emulated_ipl = ipl;
  }
}

inline uint8_t get_atari_emulated_ipl() {
  return atari_emulated_ipl;
}

inline int atari_emulating_irq(ATARI_IRQ irq) {
  return atari_emulated_irqs & (1 << irq);
}

void atari_clear_emulating_irq() {
  atari_emulated_irqs = 0;
  atari_emulated_ipl = 0;
}

inline int atari_handle_intrqr_read(uint32_t *res) {
  if (atari_emulated_irqs) {
    *res = ps_read_16(INTREQR) | atari_emulated_irqs;
    return 1;
  }
  return 0;
}

int atari_handle_intrq_write(uint32_t val) {
  if (atari_emulated_irqs && !(val & 0x8000)) {
    uint16_t previous_emulated_irqs = atari_emulated_irqs;
    uint16_t hardware_irqs_to_clear = val & ~atari_emulated_irqs;
    atari_emulated_irqs &= ~val;
    if (previous_emulated_irqs != atari_emulated_irqs) {
      atari_emulated_ipl = 0;
      for (int irq = 13; irq >= 0; irq--) {
        if (atari_emulated_irqs & (1 << irq)) {
          atari_emulated_ipl = IPL[irq];
        }
      }
    }
    if (hardware_irqs_to_clear) {
      ps_write_16(INTREQ, hardware_irqs_to_clear);
    }
    return 1;
  }
  return 0;
}
