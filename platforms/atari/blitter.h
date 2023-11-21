#ifndef BLITTER_H
#define BLITTER_H

#include <stdint.h>

/* BLiTTER registers, incs are signed, others unsigned */
#define REG_HT_RAM	    0xff8a00				/* - 0xff8a1e */

#define REG_SRC_X_INC	0xff8a20
#define REG_SRC_Y_INC	0xff8a22
#define REG_SRC_ADDR	0xff8a24

#define REG_END_MASK1	0xff8a28
#define REG_END_MASK2	0xff8a2a
#define REG_END_MASK3	0xff8a2c

#define REG_DST_X_INC	0xff8a2e
#define REG_DST_Y_INC	0xff8a30
#define REG_DST_ADDR	0xff8a32

#define REG_X_COUNT 	0xff8a36
#define REG_Y_COUNT 	0xff8a38

#define REG_BLIT_HOP	0xff8a3a				/* halftone blit operation byte */
#define REG_BLIT_LOP	0xff8a3b				/* logical blit operation byte */
#define REG_CONTROL 	0xff8a3c
#define REG_SKEW	    0xff8a3d


/* Blitter registers */
typedef struct
{
	uint32_t	src_addr;
	uint32_t	dst_addr;
	uint32_t	x_count;
	uint32_t	y_count;
	int16_t	    src_x_incr;
	int16_t	    src_y_incr;
	int16_t	    dst_x_incr;
	int16_t	    dst_y_incr;
	uint16_t	end_mask_1;
	uint16_t	end_mask_2;
	uint16_t	end_mask_3;
	uint8_t	    hop;
	uint8_t	    lop;
	uint8_t	    ctrl;
	uint8_t	    skew;

} BLITTERREGS;

/* Blitter vars */
typedef struct
{
	uint32_t	pass_cycles;
	uint32_t	op_cycles;
	uint32_t	total_cycles;

	uint32_t	buffer;
	uint32_t	x_count_reset;
	uint8_t	    hog;
	uint8_t	    smudge;
	uint8_t	    halftone_line;
	uint8_t	    fxsr;
	uint8_t	    nfsr;
	uint8_t	    skew;
} BLITTERVARS;

/* Blitter state */
typedef struct
{
	uint8_t	    fxsr;
	uint8_t	    nfsr;
	uint8_t	    have_fxsr;
	uint8_t	    need_src;
	uint8_t	    have_src;
	uint8_t	    fetch_src;
	uint8_t	    need_dst;
	uint8_t	    have_dst;

	uint16_t	src_word;
	uint16_t	dst_word;
	uint16_t	bus_word;

	uint16_t	end_mask;

	uint16_t	CountBusBlitter;				/* To count bus accesses made by the blitter */
	uint16_t	CountBusCpu;					/* To count bus accesses made by the CPU */
	uint8_t	    ContinueLater;					/* 0=false / 1=true */

} BLITTERSTATE;


#endif