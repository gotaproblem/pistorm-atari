// SPDX-License-Identifier: MIT

/*
    Code reorganized and rewritten by 
    Niklas Ekström 2021 (https://github.com/niklasekstrom)
*/

#ifndef _PS_PROTOCOL_H
#define _PS_PROTOCOL_H

#define PIN_TXN_IN_PROGRESS 0
#define PIN_IPL_ZERO 1
#define PIN_A0 2
#define PIN_A1 3
#define PIN_CLK 4
#define PIN_RESET 5
#define PIN_RD 6
#define PIN_WR 7
//#define PIN_D(x) (8 + x)

#define REG_DATA 0
#define REG_ADDR_LO 1
#define REG_ADDR_HI 2
#define REG_STATUS 3

#define STATUS_BIT_INIT 1
#define STATUS_BIT_BERR 1
#define STATUS_BIT_RESET 2

#define STATUS_MASK_IPL 0xe000
#define STATUS_SHIFT_IPL 13

//#define BCM2708_PERI_BASE 0x20000000  // pi0-1
#define BCM2708_PERI_BASE	0xFE000000  // pi4
//#define BCM2708_PERI_BASE 0x3F000000  // pi3
#define BCM2708_PERI_SIZE 0x01000000

#define GPIO_ADDR 0x200000 /* GPIO controller */
#define GPCLK_ADDR 0x101000

#define GPIO_BASE (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */
#define GPCLK_BASE (BCM2708_PERI_BASE + 0x101000)

#define CLK_PASSWD 0x5a000000
#define CLK_GP0_CTL 0x070
#define CLK_GP0_DIV 0x074

// GPIO setup macros. Always use INP_GPIO(x) before using OUT_GPIO(x) or
// SET_GPIO_ALT(x,y)
//#define INP_GPIO(g) *(gpio + ((g) / 10)) &= ~(7 << (((g) % 10) * 3))
//#define OUT_GPIO(g) *(gpio + ((g) / 10)) |= (1 << (((g) % 10) * 3))
#define SET_GPIO_ALT(g, a)  \
  *(gpio + (((g) / 10))) |= \
      (((a) <= 3 ? (a) + 4 : (a) == 4 ? 3 : 2) << (((g) % 10) * 3))

#define GPIO_PULL *(gpio + 37)      // Pull up/pull down
#define GPIO_PULLCLK0 *(gpio + 38)  // Pull up/pull down clock

#define GPFSEL0_INPUT 0x00244240 /* GPIO 2,3,6,7 always OUTPUTS - GPIO 4 = 100 = Alt Fn 0 = GPCLK0 */
#define GPFSEL1_INPUT 0x00000000 /* GPIO 10-19 all INPUTS */
#define GPFSEL2_INPUT 0x00000000 /* GPIO 20-23 all INPUTS */

#define GPFSEL0_OUTPUT 0x09244240 /* GPIO 0-3, 5-9 OUTPUTS */
#define GPFSEL1_OUTPUT 0x09249249 /* GPIO 10-19 OUTPUTS */
#define GPFSEL2_OUTPUT 0x00000249 /* GPIO 20-23 OUTPUTS */

#if (0)
#define GPFSEL_OUTPUT \
  *(gpio + 0) = GPFSEL0_OUTPUT; \
  *(gpio + 1) = GPFSEL1_OUTPUT; \
  *(gpio + 2) = GPFSEL2_OUTPUT;

#define GPFSEL_INPUT \
  *(gpio + 0) = GPFSEL0_INPUT; \
  *(gpio + 1) = GPFSEL1_INPUT; \
  *(gpio + 2) = GPFSEL2_INPUT;

#define GPIO_WRITEREG(reg, val) \
  *(gpio + 7) = (val << 8) | (reg << PIN_A0); \
  *(gpio + 7) = 1 << PIN_WR; \
  *(gpio + 10) = 1 << PIN_WR; \
  *(gpio + 10) = 0xFFFFEC;

#define GPIO_PIN_RD \
  *(gpio + 7) = (REG_DATA << PIN_A0); \
  *(gpio + 7) = 1 << PIN_RD;

#define WAIT_TXN \
  while (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS)) {}

#define END_TXN \
  *(gpio + 10) = 0xFFFFEC;
#endif
/*
static inline void ps_write_8_ex(volatile uint32_t *gpio, uint32_t address, uint32_t data) {
  if (address & 0x01) {
    data &= 0xFF; // ODD , A0=1,LDS
  } else {
    data |= (data << 8); // EVEN, A0=0,UDS
  }

  GPFSEL_OUTPUT;

  GPIO_WRITEREG(REG_DATA, (data & 0xFFFF));
  GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFF));
  GPIO_WRITEREG(REG_ADDR_HI, (0x0100 |(address >> 16)));

  GPFSEL_INPUT;

  WAIT_TXN;
}

static inline void ps_write_16_ex(volatile uint32_t *gpio, uint32_t address, uint32_t data) {
  GPFSEL_OUTPUT;

  GPIO_WRITEREG(REG_DATA, (data & 0xFFFF));
  GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFF));
  GPIO_WRITEREG(REG_ADDR_HI, (0x0000 |(address >> 16)));

  GPFSEL_INPUT;

  WAIT_TXN;
}

static inline uint32_t ps_read_8_ex(volatile uint32_t *gpio, uint32_t address) {
    GPFSEL_OUTPUT;

    GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFF));
    GPIO_WRITEREG(REG_ADDR_HI, (0x0300 |(address >> 16)));

    //asm("nop"); asm("nop");
    GPFSEL_INPUT;
    GPIO_PIN_RD;

    WAIT_TXN;
    uint32_t value = ((*(gpio + 13) >> 8) & 0xFFFF);
    END_TXN;

    if (address & 0x01)
    return value & 0xff;  // ODD , A0=1,LDS
    else
    return (value >> 8);  // EVEN, A0=0,UDS
}

static inline uint32_t ps_read_16_ex(volatile uint32_t *gpio, uint32_t address) {
    GPFSEL_OUTPUT;

    GPIO_WRITEREG(REG_ADDR_LO, (address & 0xFFFF));
    GPIO_WRITEREG(REG_ADDR_HI, (0x0200 |(address >> 16)));

    asm("nop"); asm("nop");
    GPFSEL_INPUT;
    GPIO_PIN_RD;

    WAIT_TXN;
    uint32_t value = ((*(gpio + 13) >> 8) & 0xFFFF);
    END_TXN;

    return value;
}
*/

typedef union 
{
  struct 
  {
    uint16_t lo;
    uint16_t hi;
  };
  uint32_t address;

} t_a32;

uint8_t ps_read_8(uint32_t address);
uint16_t ps_read_16(uint32_t address);
uint32_t ps_read_32(uint32_t address);
//uint8_t ps_read_8(t_a32 address);
//uint16_t ps_read_16(t_a32 address);
//uint32_t ps_read_32(t_a32 address);

void ps_write_8(uint32_t address, uint16_t data);
//void ps_write_8(t_a32 address, uint16_t data);
void ps_write_16(uint32_t address, uint16_t data);
//void ps_write_16(t_a32 address, uint16_t data);
void ps_write_32(uint32_t address, uint32_t data);
//void ps_write_32(t_a32 address, uint32_t data);

uint16_t ps_read_status_reg();
void ps_write_status_reg(unsigned int value);

void ps_setup_protocol();
void ps_reset_state_machine();
void ps_pulse_reset();

/* cryptodad */
/* cryptodad */
#define PS_CNF_CPU 0x0001
void ps_config ();
void ps_berrIAK ( uint32_t, int );

unsigned int ps_get_ipl_zero();

#define read8 ps_read_8
#define read16 ps_read_16
#define read32 ps_read_32

#define write8 ps_write_8
#define write16 ps_write_16
#define write32 ps_write_32
/*
static inline void ps_write_32_ex(volatile uint32_t *gpio, uint32_t address, uint32_t data) {
    ps_write_16_ex(gpio, address, data >> 16);
    ps_write_16_ex(gpio, address + 2, data);
}

static inline uint32_t ps_read_32_ex(volatile uint32_t *gpio, uint32_t address) {
    return (ps_read_16_ex(gpio, address) << 16) | ps_read_16_ex(gpio, address + 2);
}
*/

#define write_reg ps_write_status_reg
#define read_reg ps_read_status_reg

#define gpio_get_irq ps_get_ipl_zero


/* cryptodad */
typedef struct stats {
  uint32_t wstatus;
  uint32_t w16;
  uint32_t w8;
  uint32_t rstatus;
  uint32_t r16;
  uint32_t r8;
  uint32_t wtotal;
  uint32_t rtotal;
} t_stats;

/* cryptodad - m68kcpu.h uses this - only beneficial with 68020 */
//#define CHIP_FASTPATH
//#define FASTPATH_UPPER 0x400000 //0x200000

#endif /* _PS_PROTOCOL_H */
