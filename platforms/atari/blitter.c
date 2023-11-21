
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <endian.h>
#include "blitter.h"
#include "../../config_file/config_file.h"
#include "../../gpio/ps_protocol.h"


/*
    LOGIC OPERATIONS  
    (~s&~d) | (~s&d) | (s&~d) | (s&d)  
            |        |        |      _______________________________________
            |        |        |     |    |                                  |
    MSB     |        |        | LSB | OP | COMBINATION RULE                 |
                                    |    |                                  |
    0        0        0        0    | 0  | all zeros                        |
    0        0        0        1    | 1  | source AND destination           |
    0        0        1        0    | 2  | source AND NOT destination       |
    0        0        1        1    | 3  | source                           |
    0        1        0        0    | 4  | NOT source AND destination       |
    0        1        0        1    | 5  | destination                      |
    0        1        1        0    | 6  | source XOR destination           |
    0        1        1        1    | 7  | source OR destination            |
    1        0        0        0    | 8  | NOT source AND NOT destination   |
    1        0        0        1    | 9  | NOT source XOR destination       |
    1        0        1        0    | A  | NOT destination                  |
    1        0        1        1    | B  | source OR NOT destination        |
    1        1        0        0    | C  | NOT source                       |
    1        1        0        1    | D  | NOT source OR destination        |
    1        1        1        0    | E  | NOT source OR NOT destination    |
    1        1        1        1    | F  | all ones                         |
                                    |____|__________________________________|
            
*/


extern void *RTGbuffer;
extern uint16_t cache[4096*1024];
extern bool WTC_initialised;






/* *********************************************************************************************** */



/* Blitter logical op func */
typedef uint16_t (*BLITTER_OP_FUNC)(void);

static BLITTERREGS	BlitterRegs;
static BLITTERVARS	BlitterVars;
static BLITTERSTATE	BlitterState;
static uint16_t		BlitterHalftone[16];

static BLITTER_OP_FUNC	Blitter_ComputeHOP;
static BLITTER_OP_FUNC	Blitter_ComputeLOP;

void Blitter_Info ( void );

/*-----------------------------------------------------------------------*/
/**
 * Reset all blitter variables
 */
void Blitter_Reset ( void )
{
	BlitterRegs.src_addr = 0;
	BlitterRegs.dst_addr = 0;
	BlitterRegs.x_count = 0;
	BlitterRegs.y_count = 0;
	BlitterRegs.src_x_incr = 0;
	BlitterRegs.src_y_incr = 0;
	BlitterRegs.dst_x_incr = 0;
	BlitterRegs.dst_y_incr = 0;
	BlitterRegs.end_mask_1 = 0;
	BlitterRegs.end_mask_2 = 0;
	BlitterRegs.end_mask_3 = 0;
	BlitterRegs.hop = 0;
	BlitterRegs.lop = 0;

	BlitterRegs.ctrl = 0;//0x40;
	BlitterVars.hog = 0;//0x40;
	BlitterVars.smudge = 0;
	BlitterVars.halftone_line = 0;

	BlitterRegs.skew = 0;
	BlitterVars.fxsr = 0;
	BlitterVars.nfsr = 0;
	BlitterVars.skew = 0;

	BlitterState.fxsr = false;
	BlitterState.nfsr = false;
	BlitterState.have_fxsr = false;
	BlitterState.need_src = false;
	BlitterState.have_src = false;
	BlitterState.fetch_src = false;
	BlitterState.need_dst = false;
	BlitterState.have_dst = false;
	BlitterState.bus_word = 0;
	BlitterState.ContinueLater = 0 ;

    //ILLEGAL_ACCESS = false;
    //OP_IN_PROGRESS = false;
}





/*-----------------------------------------------------------------------*/
/**
 * Low level memory accesses to read / write a word
 * For each word access we increment the blitter's bus accesses counter.
 */
static uint16_t Blitter_ReadWord(uint32_t addr)
{
	uint16_t value;

    if ( addr >= 0x00C00000 && addr < 0x00D00000 )
        value = *(uint16_t *)(RTGbuffer + (addr - 0x00C00000));

    else
    {
        if ( WTC_initialised && addr < 0x400000 )//x3F8000 )
            value = ((cache [addr] & 0xFF) << 8) | (cache [addr + 1] & 0xFF);

        else
        {
	        value = ps_read_16 ( addr ); //STMemory_DMA_ReadWord ( addr );
            //value = be16toh ( ps_read_16 ( addr ) );//STMemory_DMA_ReadWord ( addr );
            //value = ( ps_read_8 ( addr ) << 8) | ps_read_8 ( addr + 1 );
            //printf ( "%s 0x%X 0x%X\n", __func__, addr, value );
        }
    }

    //value = ((cache [addr] & 0xFF) << 8) | (cache [addr + 1] & 0xFF);
//printf ( "%s, addr = 0x%X, value = 0x%X\n", __func__, addr, value );
	BlitterState.bus_word = value;

	return value;
}


static void Blitter_WriteWord(uint32_t addr, uint16_t value)
{
	BlitterState.bus_word = value;

    if ( addr >= 0x00C00000 && addr < 0x00D00000 )
        *(uint16_t *)(RTGbuffer + (addr - 0x00C00000)) = value;

    else
    {
        if ( WTC_initialised && addr < 0x3F8000 )
        {
            cache [addr] = value >> 8;
            cache [ addr + 1] = value & 0xFF;
        }

        else
        {
	        ps_write_16 ( addr, value ); //STMemory_DMA_WriteWord ( addr ,  value );
            //ps_write_16 ( addr, be16toh ( value ) ); //STMemory_DMA_WriteWord ( addr ,  value );
            //ps_write_8 ( addr,  value >> 8 );
            //ps_write_8 ( addr + 1, value & 0xFF );
            //printf ( "%s 0x%X 0x%X\n", __func__, addr, value );
        }
    }
}



/*-----------------------------------------------------------------------*/
/**
 * Blitter emulation - level 1 (lower level)
 */

static void Blitter_SourceShift(void)
{
	if (BlitterRegs.src_x_incr < 0)
		BlitterVars.buffer >>= 16;
	else
		BlitterVars.buffer <<= 16;
}

static void Blitter_SourceFetch( bool nfsr_on )
{
	uint32_t src_word;

	if ( !nfsr_on )
		src_word = (uint32_t)Blitter_ReadWord(BlitterRegs.src_addr);
	else
		src_word = (uint32_t)BlitterState.bus_word;

	if (BlitterRegs.src_x_incr < 0)
		BlitterVars.buffer |= (src_word << 16);
	else
		BlitterVars.buffer |= src_word;
}

static uint16_t Blitter_SourceRead(void)
{
	return (uint16_t)(BlitterVars.buffer >> BlitterVars.skew);
}

static uint16_t Blitter_DestRead(void)
{
	return BlitterState.dst_word;
}

static uint16_t Blitter_GetHalftoneWord(void)
{
	if ( BlitterVars.smudge )
		return BlitterHalftone[Blitter_SourceRead() & 15];

	else
		return BlitterHalftone[BlitterVars.halftone_line];
}


/* HOP */

static uint16_t Blitter_HOP_0(void)
{
	return 0xFFFF;
}

static uint16_t Blitter_HOP_1(void)
{
	return Blitter_GetHalftoneWord();
}

static uint16_t Blitter_HOP_2(void)
{
	return Blitter_SourceRead();
}

static uint16_t Blitter_HOP_3(void)
{
	return Blitter_SourceRead() & Blitter_GetHalftoneWord();
}

static BLITTER_OP_FUNC Blitter_HOP_Table [4] =
{
	Blitter_HOP_0,
	Blitter_HOP_1,
	Blitter_HOP_2,
	Blitter_HOP_3
};

static void Blitter_Select_HOP(void)
{
	Blitter_ComputeHOP = Blitter_HOP_Table[BlitterRegs.hop];
}

/* end HOP */

/* LOP */

static uint16_t Blitter_LOP_0(void)
{
	return 0;
}

static uint16_t Blitter_LOP_1(void)
{
	return Blitter_ComputeHOP() & Blitter_DestRead();
}

static uint16_t Blitter_LOP_2(void)
{
	return Blitter_ComputeHOP() & ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_3(void)
{
	return Blitter_ComputeHOP();
}

static uint16_t Blitter_LOP_4(void)
{
	return ~Blitter_ComputeHOP() & Blitter_DestRead();
}

static uint16_t Blitter_LOP_5(void)
{
	return Blitter_DestRead();
}

static uint16_t Blitter_LOP_6(void)
{
	return Blitter_ComputeHOP() ^ Blitter_DestRead();
}

static uint16_t Blitter_LOP_7(void)
{
	return Blitter_ComputeHOP() | Blitter_DestRead();
}

static uint16_t Blitter_LOP_8(void)
{
	return ~Blitter_ComputeHOP() & ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_9(void)
{
	return ~Blitter_ComputeHOP() ^ Blitter_DestRead();
}

static uint16_t Blitter_LOP_A(void)
{
	return ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_B(void)
{
	return Blitter_ComputeHOP() | ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_C(void)
{
	return ~Blitter_ComputeHOP();
}

static uint16_t Blitter_LOP_D(void)
{
	return ~Blitter_ComputeHOP() | Blitter_DestRead();
}

static uint16_t Blitter_LOP_E(void)
{
	return ~Blitter_ComputeHOP() | ~Blitter_DestRead();
}

static uint16_t Blitter_LOP_F(void)
{
	return 0xFFFF;
}

static const struct {
	BLITTER_OP_FUNC	lop_func;
	uint8_t		need_src;
	uint8_t		need_dst;
} Blitter_LOP_Table [16] =
{
	{ Blitter_LOP_0, false, false } ,
	{ Blitter_LOP_1, true,	true },
	{ Blitter_LOP_2, true,	true },
	{ Blitter_LOP_3, true,	false },
	{ Blitter_LOP_4, true,	true },
	{ Blitter_LOP_5, false,	true },
	{ Blitter_LOP_6, true,	true },
	{ Blitter_LOP_7, true,	true },
	{ Blitter_LOP_8, true,	true },
	{ Blitter_LOP_9, true,	true },
	{ Blitter_LOP_A, false,	true },
	{ Blitter_LOP_B, true,	true },
	{ Blitter_LOP_C, true,	false },
	{ Blitter_LOP_D, true,	true },
	{ Blitter_LOP_E, true,	true },
	{ Blitter_LOP_F, false,	false }
};

static void Blitter_Select_LOP(void)
{
	Blitter_ComputeLOP = Blitter_LOP_Table[BlitterRegs.lop].lop_func;
}

/* end LOP */


static void Blitter_ProcessWord(void)
{
	uint16_t	lop;
	uint16_t	dst_data;


	/* Do FXSR if needed (only if src is used) */
	if ( BlitterState.fxsr && !BlitterState.have_fxsr && BlitterState.need_src )
	{
		Blitter_SourceShift();
		Blitter_SourceFetch( false );
		BlitterRegs.src_addr = BlitterRegs.src_addr + BlitterRegs.src_x_incr;		/* always increment src_addr after doing the FXSR */
		BlitterState.have_fxsr = true;
	}

	/* Read src if needed */
	if ( BlitterState.need_src && !BlitterState.have_src )
	{
		if ( !BlitterState.nfsr )
		{
			Blitter_SourceShift();
			Blitter_SourceFetch( false );
			BlitterState.have_src = true;
			BlitterState.fetch_src = true;
		}
	}

	/* Read dst if needed */
	if ( BlitterState.need_dst && !BlitterState.have_dst )
	{
		BlitterState.dst_word = Blitter_ReadWord(BlitterRegs.dst_addr);
		BlitterState.have_dst = true;
	}

	/* Special 'weird' case for x_count=1 and NFSR=1 */
	if ( ( BlitterVars.nfsr ) && ( BlitterRegs.x_count == 1 ) )
	{
		Blitter_SourceShift();
		Blitter_SourceFetch( true );
	}

	lop = Blitter_ComputeLOP();

	/* When mask is not all '1', a read-modify-write is always performed */
	/* NOTE : Atari's doc wrongly states that NFSR can also do a RMW, but only mask can */
	/* (cf http://www.atari-forum.com/viewtopic.php?f=16&t=38157) */
	if ( BlitterState.end_mask != 0xFFFF )
		dst_data = (lop & BlitterState.end_mask) | (Blitter_DestRead() & ~BlitterState.end_mask);
	else
		dst_data = lop;

	Blitter_WriteWord(BlitterRegs.dst_addr, dst_data);

	/* Special 'weird' case for x_count=1 and NFSR=1 */
	if ( ( BlitterVars.nfsr ) && ( BlitterRegs.x_count == 1 ) )
	{
		Blitter_SourceShift();
		Blitter_SourceFetch( true );
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Blitter emulation - level 2 (higher level)
 *
 * If BlitterState.ContinueLater==1, it means we're resuming from a previous
 * Blitter_ProcessWord() call that did not complete because we reached
 * maximum number of bus accesses. In that case, we continue from the latest
 * state, keeping the values we already have for src_word and dst_word.
 */

/*
 * Reset internal states after fully processing 1 word or when blitter is started
 */
static void Blitter_FlushWordState( bool FlushFxsr )
{
	if ( FlushFxsr )
		BlitterState.have_fxsr = false;

	BlitterState.have_src = false;
	BlitterState.fetch_src = false;
	BlitterState.have_dst = false;
}


/**
 * Process 1 word for the current x_count/y_count values.
 * Update addresses/counters/states when done
 * If too many bus accesses were made in non-hog mode, we return
 * and we resume later from the same states.
 */
static void Blitter_Step(void)
{
	bool	FirstWord;

	/* Check if this is the first word of a line */
	FirstWord = ( BlitterRegs.x_count == BlitterVars.x_count_reset );

	/* Set mask for this word (order of 'if' matters) */
	if ( FirstWord || ( BlitterVars.x_count_reset == 1 ) )		/* 1st word or single word line */
		BlitterState.end_mask = BlitterRegs.end_mask_1;
	else if ( BlitterRegs.x_count == 1 )				/* last word for non-single word line */
		BlitterState.end_mask = BlitterRegs.end_mask_3;
	else								/* middle word for non-single word line */
		BlitterState.end_mask = BlitterRegs.end_mask_2;

	/* Set internal nfsr=0 by default at the start of a new line (it will be updated if needed when xcount goes from 2 to 1) */
	if ( FirstWord )
		BlitterState.nfsr = 0;

	/* Read an extra word at the start of a line if FXSR is set */
	/* This extra word will only be read if the blitter LOP/HOP needs to read src */
	if ( FirstWord )
		BlitterState.fxsr = BlitterVars.fxsr;

	/* Check if this operation requires to read src */
	BlitterState.need_src = Blitter_LOP_Table[BlitterRegs.lop].need_src;
	/* Check if HOP uses src : bit1==1 or halftone with smudge bit */
	BlitterState.need_src = BlitterState.need_src && ( ( BlitterRegs.hop & 2 ) || ( ( BlitterRegs.hop == 1 ) && BlitterVars.smudge ) );

	/* Check if this operation requires to read dst (if mask != 0xFFFF, read dst will be forced to do a read-modify-write */
	BlitterState.need_dst = Blitter_LOP_Table[BlitterRegs.lop].need_dst || ( BlitterState.end_mask != 0xFFFF );


	/* Call main function to process the data */
	/* Read src/dst/halftone (if needed) + process + write to dst */
	Blitter_ProcessWord();


	/* Write was done, update counters/addresses/states for next step */
	/* Take NFSR value into account (this must be checked when x_count=2, as on real blitter) */
	if ( ( BlitterRegs.x_count == 2 ) && BlitterVars.nfsr )
		BlitterState.nfsr = 1;					/* next source read will be ignored in Blitter_SourceRead() */

	/* Update source address if a word was read from src */
	if ( BlitterState.fetch_src )
	{
		/* If this was the last read of a line or if last read will be ignored, then we go to the next source line */
		if ( ( BlitterRegs.x_count == 1 ) || ( BlitterState.nfsr == 1 ) )
			BlitterRegs.src_addr += BlitterRegs.src_y_incr;
		else
			BlitterRegs.src_addr = BlitterRegs.src_addr + BlitterRegs.src_x_incr;

        //if ( BlitterRegs.src_addr > 0x00FFFFFE )
        //    BlitterRegs.src_addr = BlitterRegs.src_addr & 0x00FFFFFF;
	}

	/* Update X/Y count as well as dest address */
	if ( BlitterRegs.x_count == 1 )					/* end of line reached */
	{
		BlitterState.have_fxsr = false;
		BlitterRegs.y_count--;
		BlitterRegs.x_count = BlitterVars.x_count_reset;

		BlitterRegs.dst_addr = BlitterRegs.dst_addr + BlitterRegs.dst_y_incr;

        //if ( BlitterRegs.dst_addr > 0x00FFFFFE )
        //    BlitterRegs.dst_addr = BlitterRegs.dst_addr & 0x00FFFFFF;

		if ( BlitterRegs.dst_y_incr >= 0 )
			BlitterVars.halftone_line = ( BlitterVars.halftone_line + 1 ) & 15;
		else
			BlitterVars.halftone_line = ( BlitterVars.halftone_line - 1 ) & 15;
	}
	
	else								/* continue on the same line */
	{
		BlitterRegs.x_count--;
		BlitterRegs.dst_addr = BlitterRegs.dst_addr + BlitterRegs.dst_x_incr;

        //if ( BlitterRegs.dst_addr > 0x00FFFFFE )
        //    BlitterRegs.dst_addr = BlitterRegs.dst_addr & 0x00FFFFFF;
	}
//printf ( "%s Xcount = %d, Ycount = %d\n", __func__, BlitterRegs.x_count, BlitterRegs.y_count );
	/* ProcessWord is complete, reset internal content of src/dst words */
	Blitter_FlushWordState ( false );
}


/*-----------------------------------------------------------------------*/
/**
 * Start/Resume the blitter
 *
 * Note that in non-hog mode, the blitter only runs for 64 bus cycles
 * before giving the bus back to the CPU. Due to this mode, this function must
 * be able to abort and resume the blitting at any time, keeping the same internal states.
 * - In cycle exact mode, the blitter will have 64 bus accesses and the cpu 64 bus accesses
 * - In non cycle exact mode, the blitter will have 64 bus accesses and the cpu
 *   will run during 64*4 = 256 cpu cycles
 */

//extern short flushstatereq;


static void Blitter_Start(void)
{
    static uint8_t interrupt;

    Blitter_Info ();

//fprintf ( stderr , "blitter start %d video_cyc=%d %d@%d\n" , nCyclesMainCounter , FrameCycles , LineCycles, HblCounterVideo );
//printf (  "blitter start addr=%x dst=%x xcount=%d ycount=%d fxsr=%d nfsr=%d skew=%d src_x_incr=%d src_y_incr=%d\n" , BlitterRegs.src_addr ,BlitterRegs.dst_addr, BlitterRegs.x_count , BlitterRegs.y_count , BlitterVars.fxsr , BlitterVars.nfsr , BlitterVars.skew , BlitterRegs.src_x_incr , BlitterRegs.src_y_incr );

	/* Select HOP & LOP funcs */
	Blitter_Select_HOP();
	Blitter_Select_LOP();    

	/* Busy=1, set line to high/1 and clear interrupt */
	//MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_GPU_DONE , MFP_GPIP_STATE_HIGH );
    interrupt = ps_read_8 ( 0xFFFA01 );
    ps_write_8 ( 0xFFFA01, interrupt |= 0x08 );

	/* Now we enter the main blitting loop */
	do
	{
		Blitter_Step();
	}
	while ( BlitterRegs.y_count > 0 && BlitterVars.hog );


	BlitterRegs.ctrl = (BlitterRegs.ctrl & 0xF0) | BlitterVars.halftone_line;

    /* Should only clear busy bit when Y count is zero */
	if (BlitterRegs.y_count == 0)
	{
		/* Blit complete, clear busy and hog bits */
		BlitterRegs.ctrl &= ~(0x80|0x40);

		/* Busy=0, set line to low/0 and request interrupt */
		//MFP_GPIP_Set_Line_Input ( pMFP_Main , MFP_GPIP_LINE_GPU_DONE , MFP_GPIP_STATE_LOW );
        interrupt = ps_read_8 ( 0x00FFFA01 );
      
        if ( interrupt & 0x08 )
            ps_write_8 ( 0x00FFFA01, interrupt &= 0xF7 );
	}
}




#if (0)
/*-----------------------------------------------------------------------*/
/**
 * Read blitter halftone ram.
 */
static void Blitter_Halftone_ReadWord(int index)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_16 (REG_HT_RAM + index + index, BlitterHalftone[index]);
}

void Blitter_Halftone00_ReadWord(void) { Blitter_Halftone_ReadWord(0); }
void Blitter_Halftone01_ReadWord(void) { Blitter_Halftone_ReadWord(1); }
void Blitter_Halftone02_ReadWord(void) { Blitter_Halftone_ReadWord(2); }
void Blitter_Halftone03_ReadWord(void) { Blitter_Halftone_ReadWord(3); }
void Blitter_Halftone04_ReadWord(void) { Blitter_Halftone_ReadWord(4); }
void Blitter_Halftone05_ReadWord(void) { Blitter_Halftone_ReadWord(5); }
void Blitter_Halftone06_ReadWord(void) { Blitter_Halftone_ReadWord(6); }
void Blitter_Halftone07_ReadWord(void) { Blitter_Halftone_ReadWord(7); }
void Blitter_Halftone08_ReadWord(void) { Blitter_Halftone_ReadWord(8); }
void Blitter_Halftone09_ReadWord(void) { Blitter_Halftone_ReadWord(9); }
void Blitter_Halftone10_ReadWord(void) { Blitter_Halftone_ReadWord(10); }
void Blitter_Halftone11_ReadWord(void) { Blitter_Halftone_ReadWord(11); }
void Blitter_Halftone12_ReadWord(void) { Blitter_Halftone_ReadWord(12); }
void Blitter_Halftone13_ReadWord(void) { Blitter_Halftone_ReadWord(13); }
void Blitter_Halftone14_ReadWord(void) { Blitter_Halftone_ReadWord(14); }
void Blitter_Halftone15_ReadWord(void) { Blitter_Halftone_ReadWord(15); }
#endif

#if (0)
/*-----------------------------------------------------------------------*/
/**
 * Read blitter source x increment (0xff8a20).
 */
void Blitter_SourceXInc_ReadWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_16 (REG_SRC_X_INC, (uint16_t)(BlitterRegs.src_x_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter source y increment (0xff8a22).
 */
void Blitter_SourceYInc_ReadWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_16 (REG_SRC_Y_INC, (uint16_t)(BlitterRegs.src_y_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter source address (0xff8a24).
 */
void Blitter_SourceAddr_ReadLong(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_32 (REG_SRC_ADDR, BlitterRegs.src_addr);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 1.
 */
void Blitter_Endmask1_ReadWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_16 (REG_END_MASK1, BlitterRegs.end_mask_1);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 2.
 */
void Blitter_Endmask2_ReadWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_16 (REG_END_MASK2, BlitterRegs.end_mask_2);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter endmask 3.
 */
void Blitter_Endmask3_ReadWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_16 (REG_END_MASK3, BlitterRegs.end_mask_3);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination x increment (0xff8a2E).
 */
void Blitter_DestXInc_ReadWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_16 (REG_DST_X_INC, (uint16_t)(BlitterRegs.dst_x_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination y increment (0xff8a30).
 */
void Blitter_DestYInc_ReadWord(void)
{
	if ( ILLEGAL_ACCESS)
		return;						/* Ignore access */

	ps_write_16 (REG_DST_Y_INC, (uint16_t)(BlitterRegs.dst_y_incr));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter destination address.
 */
void Blitter_DestAddr_ReadLong(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_32 (REG_DST_ADDR, BlitterRegs.dst_addr);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter words-per-line register X count.
 */
void Blitter_WordsPerLine_ReadWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	ps_write_16 (REG_X_COUNT, (uint16_t)(BlitterRegs.x_count & 0xFFFF));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter lines-per-bitblock register Y count.
 */
void Blitter_LinesPerBitblock_ReadWord(void)
{
	if ( ILLEGAL_ACCESS)
		return;						/* Ignore access */

	ps_write_16 (REG_Y_COUNT, (uint16_t)(BlitterRegs.y_count & 0xFFFF));
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter halftone operation register.
 */
void Blitter_HalftoneOp_ReadByte(void)
{
	ps_write_8 (REG_BLIT_HOP, BlitterRegs.hop);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter logical operation register.
 */
void Blitter_LogOp_ReadByte(void)
{
	ps_write_8 (REG_BLIT_LOP, BlitterRegs.lop);
}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter control register.
 */
//void Blitter_Control_ReadByte(void)
//{
	/* busy, hog/blit, smudge, n/a, 4 bits for halftone line number */
//	ps_write_8 (REG_CONTROL, BlitterRegs.ctrl);
//}

/*-----------------------------------------------------------------------*/
/**
 * Read blitter skew register.
 */
//void Blitter_Skew_ReadByte(void)
//{
//	ps_write_8 (REG_SKEW, BlitterRegs.skew);
//}
#endif 

#if (0)
/*-----------------------------------------------------------------------*/
/**
 * Write to blitter halftone ram.
 */
static void Blitter_Halftone_WriteWord(int index)
{
	if ( ILLEGAL_ACCESS)
		return;						/* Ignore access */

	BlitterHalftone[index] = ps_read_16 (REG_HT_RAM + index + index);
}

void Blitter_Halftone00_WriteWord(void) { Blitter_Halftone_WriteWord(0); }
void Blitter_Halftone01_WriteWord(void) { Blitter_Halftone_WriteWord(1); }
void Blitter_Halftone02_WriteWord(void) { Blitter_Halftone_WriteWord(2); }
void Blitter_Halftone03_WriteWord(void) { Blitter_Halftone_WriteWord(3); }
void Blitter_Halftone04_WriteWord(void) { Blitter_Halftone_WriteWord(4); }
void Blitter_Halftone05_WriteWord(void) { Blitter_Halftone_WriteWord(5); }
void Blitter_Halftone06_WriteWord(void) { Blitter_Halftone_WriteWord(6); }
void Blitter_Halftone07_WriteWord(void) { Blitter_Halftone_WriteWord(7); }
void Blitter_Halftone08_WriteWord(void) { Blitter_Halftone_WriteWord(8); }
void Blitter_Halftone09_WriteWord(void) { Blitter_Halftone_WriteWord(9); }
void Blitter_Halftone10_WriteWord(void) { Blitter_Halftone_WriteWord(10); }
void Blitter_Halftone11_WriteWord(void) { Blitter_Halftone_WriteWord(11); }
void Blitter_Halftone12_WriteWord(void) { Blitter_Halftone_WriteWord(12); }
void Blitter_Halftone13_WriteWord(void) { Blitter_Halftone_WriteWord(13); }
void Blitter_Halftone14_WriteWord(void) { Blitter_Halftone_WriteWord(14); }
void Blitter_Halftone15_WriteWord(void) { Blitter_Halftone_WriteWord(15); }
#endif

#if (0)
/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source x increment.
 */
void Blitter_SourceXInc_WriteWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	BlitterRegs.src_x_incr = (short)(ps_read_16 (REG_SRC_X_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source y increment.
 */
void Blitter_SourceYInc_WriteWord(void)
{
	if ( ILLEGAL_ACCESS)
		return;						/* Ignore access */

	BlitterRegs.src_y_incr = (short)(ps_read_16 (REG_SRC_Y_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source address register (0xff8a24).
 */
void Blitter_SourceAddr_WriteLong(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	//if ( ConfigureParams.System.bAddressSpace24 == true )
		BlitterRegs.src_addr = ps_read_32 (REG_SRC_ADDR) & 0x00FFFFFE;	/* Normal STF/STE */
	//else
	//	BlitterRegs.src_addr = ps_read_32 (REG_SRC_ADDR) & 0xFFFFFFFE;	/* Falcon with extra TT RAM */
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 1.
 */
void Blitter_Endmask1_WriteWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	BlitterRegs.end_mask_1 = ps_read_16 (REG_END_MASK1);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 2.
 */
void Blitter_Endmask2_WriteWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	BlitterRegs.end_mask_2 = ps_read_16 (REG_END_MASK2);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter endmask 3.
 */
void Blitter_Endmask3_WriteWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	BlitterRegs.end_mask_3 = ps_read_16 (REG_END_MASK3);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter destination x increment.
 */
void Blitter_DestXInc_WriteWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	BlitterRegs.dst_x_incr = (short)(ps_read_16 (REG_DST_X_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter source y increment.
 */
void Blitter_DestYInc_WriteWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	BlitterRegs.dst_y_incr = (short)(ps_read_16 (REG_DST_Y_INC) & 0xFFFE);
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter destination address register.
 */
void Blitter_DestAddr_WriteLong(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	//if ( ConfigureParams.System.bAddressSpace24 == true )
		BlitterRegs.dst_addr = ps_read_32 (REG_DST_ADDR) & 0x00FFFFFE;	/* Normal STF/STE */
	//else
	//	BlitterRegs.dst_addr = ps_read_32 (REG_DST_ADDR) & 0xFFFFFFFE;	/* Falcon with extra TT RAM */
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter words-per-line register X count.
 */
void Blitter_WordsPerLine_WriteWord(void)
{
	if ( ILLEGAL_ACCESS)
		return;						/* Ignore access */

	uint32_t x_count = (uint32_t)ps_read_16 (REG_X_COUNT);

	if (x_count == 0)
		x_count = 65536;

	BlitterRegs.x_count = x_count;
	BlitterVars.x_count_reset = x_count;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter lines-per-bitblock register Y count.
 */
void Blitter_LinesPerBitblock_WriteWord(void)
{
	if ( ILLEGAL_ACCESS )
		return;						/* Ignore access */

	uint32_t y_count = (uint32_t)ps_read_16 (REG_Y_COUNT);

	if (y_count == 0)
		y_count = 65536;

	BlitterRegs.y_count = y_count;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter halftone operation register.
 */
void Blitter_HalftoneOp_WriteByte(void)
{
	/* h/ware reg masks out the top 6 bits! */
	BlitterRegs.hop = ps_read_8 (REG_BLIT_HOP) & 3;
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter logical operation register.
 */
void Blitter_LogOp_WriteByte(void)
{
	/* h/ware reg masks out the top 4 bits! */
	BlitterRegs.lop = ps_read_8 (REG_BLIT_LOP) & 0xF;
}
#endif

#if (0)
/*-----------------------------------------------------------------------*/
/**
 * Write to blitter control register.
 */
void Blitter_Control_WriteByte(void)
{
	/* Control register bits:
	 * bit 7 : start/stop bit (write) - busy bit (read)
	 *	- Turn on Blitter activity and stay "1" until copy finished
	 * bit 6 : Blit-mode bit
	 *	- 0: Blit mode, CPU and Blitter get 64 bus accesses in turns
	 *	- 1: HOG Mode, Blitter reserves and hogs the bus for as long
	 *      as the copy takes, CPU and DMA get no Bus access
	 * bit 5 : Smudge mode
	 * 	Which line of the halftone pattern to start with is read from
	 *	the first source word when the copy starts
	 * bit 4 : not used
	 * bits 0-3 :
	 *	The lowest 4 bits contain the halftone pattern line number
	 */

	BlitterRegs.ctrl = ps_read_8 (REG_CONTROL) & 0xEF;

	BlitterVars.hog = BlitterRegs.ctrl & 0x40;
	BlitterVars.smudge = BlitterRegs.ctrl & 0x20;
	BlitterVars.halftone_line = BlitterRegs.ctrl & 0xF;

	/* Remove old pending update interrupt */
	//CycInt_RemovePendingInterrupt(INTERRUPT_BLITTER);

	/* Start/Stop bit set ? */
	if (BlitterRegs.ctrl & 0x80)
	{
		if (BlitterRegs.y_count == 0)
		{
			/* Blitter transfer is already complete, clear busy and hog bits */
			BlitterRegs.ctrl &= ~(0x80|0x40);			// TODO : check on real STE, does it clear hog bit too ?
		}
		
	}	
}

/*-----------------------------------------------------------------------*/
/**
 * Write to blitter skew register.
 */
void Blitter_Skew_WriteByte(void)
{
	BlitterRegs.skew = ps_read_8 (REG_SKEW);
	BlitterVars.fxsr = (BlitterRegs.skew & 0x80)?1:0;
	BlitterVars.nfsr = (BlitterRegs.skew & 0x40)?1:0;
	BlitterVars.skew = BlitterRegs.skew & 0xF;
}


/*-----------------------------------------------------------------------*/
/**
 * Handler which continues blitting after 64 bus cycles in non-CE mode
 */
void Blitter_InterruptHandler(void)
{
	//CycInt_AcknowledgeInterrupt();

	if (BlitterRegs.ctrl & 0x80)
	{
		Blitter_Start();
	}
}
#endif




/* -------------------------------- */

void blitInit ( void )
{
    Blitter_Reset ();
}


int blitRead ( uint8_t type, uint32_t addr, uint32_t *result )
{
    //ILLEGAL_ACCESS = false;

    //if ( type == OP_TYPE_BYTE )
    //    ILLEGAL_ACCESS = true;

    switch ( addr )
    {
        /* Half-Tone RAM 16 words */
        case 0x00FF8A00:
        case 0x00FF8A02:
        case 0x00FF8A04:
        case 0x00FF8A06:
        case 0x00FF8A08:
        case 0x00FF8A0A:
        case 0x00FF8A0C:
        case 0x00FF8A0E:
        case 0x00FF8A10:
        case 0x00FF8A12:
        case 0x00FF8A14:
        case 0x00FF8A16:
        case 0x00FF8A18:
        case 0x00FF8A1A:
        case 0x00FF8A1C:
        case 0x00FF8A1E:

           // *result = BlitterHalftone [BlitterVars.halftone_line];
            *result = BlitterHalftone [(addr & 0xFF) >> 1];
          
            break;

        case 0x00FF8A20:
            *result = BlitterRegs.src_x_incr;
            break;

        case 0x00FF8A22:
            *result = BlitterRegs.src_y_incr;
            break;

        case 0x00FF8A24:
            *result = BlitterRegs.src_addr;
            break;
        
        case 0x00FF8A28:
            *result = BlitterRegs.end_mask_1;
            break;

        case 0x00FF8A2A:
            *result = BlitterRegs.end_mask_2;
            break;

        case 0x00FF8A2C:
            *result = BlitterRegs.end_mask_3;
            break;

        case 0x00FF8A2E:
            *result = BlitterRegs.dst_x_incr;
            break;

        case 0x00FF8A30:
            *result = BlitterRegs.dst_y_incr;
            break;

        case 0x00FF8A32:
            *result = BlitterRegs.dst_addr;
            break;

        case 0x00FF8A36:
            *result = BlitterRegs.x_count;
            break;

        case 0x00FF8A38:
            *result = BlitterRegs.y_count;
            break;

        case 0x00FF8A3A:
            *result = BlitterRegs.hop;
            break;
            
        case 0x00FF8A3B:
            *result = BlitterRegs.lop;
            break;

        case 0x00FF8A3C:
            //if ( OP_IN_PROGRESS )
            //{
            //    *result = BlitterRegs.ctrl & 0x7F;
            //    OP_IN_PROGRESS = false;
            //}

            //else
                *result = BlitterRegs.ctrl;

            break;

        case 0x00FF8A3D:
            *result = BlitterRegs.skew;
            break;
    }

    return 1;
}


int blitWrite ( uint8_t type, uint32_t addr, uint32_t value )
{
   // static uint32_t srcoffset, dstoffset;

    //ILLEGAL_ACCESS = false;

   // if ( type == OP_TYPE_BYTE )
    //    ILLEGAL_ACCESS = true;

    switch ( addr )
    {
        case 0x00FF8A00:
        case 0x00FF8A02:
        case 0x00FF8A04:
        case 0x00FF8A06:
        case 0x00FF8A08:
        case 0x00FF8A0A:
        case 0x00FF8A0C:
        case 0x00FF8A0E:
        case 0x00FF8A10:
        case 0x00FF8A12:
        case 0x00FF8A14:
        case 0x00FF8A16:
        case 0x00FF8A18:
        case 0x00FF8A1A:
        case 0x00FF8A1C:
        case 0x00FF8A1E:

            BlitterHalftone [(addr & 0xFF) >> 1] = value;//(value & 0xFFFF);
            
            break;

        case 0x00FF8A20:

            if ( BlitterRegs.x_count  == 1 )
                BlitterRegs.src_x_incr = 0;

            else
                BlitterRegs.src_x_incr = value;//(int16_t)(value & 0x7FFE);

            break;

        case 0x00FF8A22:

            BlitterRegs.src_y_incr = value;//(int16_t)(value & 0x7FFE);

            break;

        /* Source Address - Hi 16 bits */
        case 0x00FF8A24:
            
            //BlitterRegs.src_addr = value & 0x00FFFFFE;

            if ( type == OP_TYPE_LONGWORD )
            {
                BlitterRegs.src_addr = (value & 0x00FFFFFE);
                //printf ( "0x00FF8A24 LW = 0x%X\n", value );
            }
/*
            else if ( type == OP_TYPE_WORD )
            {
                //BlitterRegs.src_addr = ( (value & 0xFF) << 16 ) | (BlitterRegs.src_addr & 0xFFFF);
                BlitterRegs.src_addr = (value << 16) | (BlitterRegs.src_addr & 0xFFFE);
                //printf ( "0x00FF8A24 W = 0x%X\n", value );
            }

            else
                printf ( "0x00FF8A24 SRC unexpected BYTE\n" );

            //printf ( "SRC ADDRESS is 0x%X\n", blit_p->SrcAddress );
*/
            break;

        /* Source Address - Lo 16 bits */
        case 0x00FF8A26:

            if ( type == OP_TYPE_WORD )
			{
                //BlitterRegs.src_addr = (BlitterRegs.src_addr & 0x00FF) | (value & 0xFFFE);
                BlitterRegs.src_addr = ((BlitterRegs.src_addr & 0x00FF0000) | (value & 0xFFFE));
			}
            //else
            //    printf ( "0x00FF8A26 SRC unexpected - type %d addr = 0x%X\n", type, BlitterRegs.src_addr  );

            break;
        
        case 0x00FF8A28:
            BlitterRegs.end_mask_1 = value;
            //printf ( "end_mask_1 addr 0x%X = 0x%X\n", addr, value );
            break;

        case 0x00FF8A2A:
            BlitterRegs.end_mask_2 = value;
           // printf ( "end_mask_2 addr 0x%X = 0x%X\n", addr, value );
            break;

        case 0x00FF8A2C:
            BlitterRegs.end_mask_3 = value;
           // printf ( "end_mask_3 addr 0x%X = 0x%X\n", addr, value );
            break;

        case 0x00FF8A2E:
            BlitterRegs.dst_x_incr  = value;//(int16_t)(value & 0x7FFE);
            break;

        case 0x00FF8A30:
            BlitterRegs.dst_y_incr  = value;//(int16_t)(value & 0x7FFE);
            break;

        /* Destination Address - Hi 16 bits */
        case 0x00FF8A32:

            if ( type == OP_TYPE_LONGWORD )
            {
                BlitterRegs.dst_addr = (value & 0x00FFFFFE);
                //printf ( "0x00FF8A32 LW = 0x%X\n", value );
            }
/*
            else if ( type == OP_TYPE_WORD )
            {
                //BlitterRegs.dst_addr = ( (value & 0xFF) << 16 ) | (BlitterRegs.dst_addr & 0xFFFF);
                BlitterRegs.dst_addr = (value << 16) | (BlitterRegs.dst_addr & 0xFFFE);
                //printf ( "0x00FF8A32 W = 0x%X\n", value );
            }

            //printf ( "0x00FF8A32 DST address = 0x%X\n", BlitterRegs.dst_addr );
*/
            break;

        /* Destination Address - Lo 16 bits */
        case 0x00FF8A34:

            if ( type == OP_TYPE_WORD )
            {
                BlitterRegs.dst_addr = ((BlitterRegs.dst_addr & 0x00FF0000) | (value & 0xFFFE));
                //printf ( "0x00FF8A34 W = 0x%X\n", value );
            }
/*
            else if ( type == OP_TYPE_LONGWORD )
            {
                //BlitterRegs.dst_addr = (BlitterRegs.dst_addr & 0x00FF0000) | (value & 0xFFFE);
                //printf ( "DST ADD LW unexpected 0x%X\n", value );
                //BlitterRegs.dst_addr = (BlitterRegs.dst_addr & 0x00FF0000) | ((value >> 16) & 0xFFFE);

                //value &= 0x1FFFF;
               // BlitterRegs.x_count = value;
                //BlitterVars.x_count_reset = value;
            }

            //else
            //    printf ( "0x00FF8A34 = 0x%X type %d\n", value, type );

            //printf ( "0x00FF8A34 DST address = 0x%X\n", BlitterRegs.dst_addr );
*/
            break;

        case 0x00FF8A36:

			value &= 0x1FFFF;
			
            if ( value == 0 )
                value = 65536;

            
            BlitterRegs.x_count = value;
            BlitterVars.x_count_reset = value;

            break;

        case 0x00FF8A38:

			value &= 0x1FFFF;

            if ( value == 0 )
                value = 65536;

            
            BlitterRegs.y_count = value;

            break;

        /* HALFTONE OPERATION */
        /* 
           0 All Ones
           1 halftone
           2 source
           3 source & halftone
        */
        case 0x00FF8A3A:
            BlitterRegs.hop = value & 0x03;
            break;
            
        case 0x00FF8A3B:

            BlitterRegs.lop = value & 0x0F;
            //OP_IN_PROGRESS = true;

            //Blitter_Info ();

            Blitter_Start ();

            break;

        case 0x00FF8A3C:
            
            BlitterRegs.ctrl = value & 0xEF;

            BlitterVars.hog = BlitterRegs.ctrl & 0x40;
            BlitterVars.smudge = BlitterRegs.ctrl & 0x20;
            BlitterVars.halftone_line = BlitterRegs.ctrl & 0x0F;

            /* Remove old pending update interrupt */
            //CycInt_RemovePendingInterrupt(INTERRUPT_BLITTER);

            /* Start/Stop bit set ? */
            /* Busy bit should only be cleared if Y count is zero */
            if (BlitterRegs.ctrl & 0x80)
            {
                if (BlitterRegs.y_count == 0)
                {
                    /* Blitter transfer is already complete, clear busy and hog bits */
                    BlitterRegs.ctrl &= ~(0x80|0x40);			// TODO : check on real STE, does it clear hog bit too ?
                }
                
                /* still Busy so continue with Blit */
                else 
                    Blitter_Start ();
            }

            break;

        case 0x00FF8A3D:

            BlitterRegs.skew = value & 0xCF;
            
            BlitterVars.fxsr = (BlitterRegs.skew & 0x80)?1:0;
	        BlitterVars.nfsr = (BlitterRegs.skew & 0x40)?1:0;
	        BlitterVars.skew = BlitterRegs.skew & 0xF;

            break;
    }
    
}


void Blitter_Info ( void )
{
	BLITTERREGS *regs = &BlitterRegs;

	printf( "src addr  (0x%x): 0x%06x\n", REG_SRC_ADDR, regs->src_addr);
	printf( "dst addr  (0x%x): 0x%06x\n", REG_DST_ADDR, regs->dst_addr);
	printf( "x count   (0x%x): %u\n",     REG_X_COUNT, regs->x_count);
	printf( "y count   (0x%x): %u\n",     REG_Y_COUNT, regs->y_count);
	printf( "src X-inc (0x%x): %hd\n",    REG_SRC_X_INC, regs->src_x_incr);
	printf( "src Y-inc (0x%x): %hd\n",    REG_SRC_Y_INC, regs->src_y_incr);
	printf( "dst X-inc (0x%x): %hd\n",    REG_DST_X_INC, regs->dst_x_incr);
	printf( "dst Y-inc (0x%x): %hd\n",    REG_DST_Y_INC, regs->dst_y_incr);
	printf( "end mask1 (0x%x): 0x%04x\n", REG_END_MASK1, regs->end_mask_1);
	printf( "end mask2 (0x%x): 0x%04x\n", REG_END_MASK2, regs->end_mask_2);
	printf( "end mask3 (0x%x): 0x%04x\n", REG_END_MASK3, regs->end_mask_3);
	printf( "HOP       (0x%x): 0x%02x\n", REG_BLIT_HOP, regs->hop);
	printf( "LOP       (0x%x): 0x%02x\n", REG_BLIT_LOP, regs->lop);
	/* List control bits: busy, hog/blit, smudge, n/a, 4 bits for halftone line number ? */
	printf( "control   (0x%x): 0x%02x\n", REG_CONTROL, regs->ctrl);
	printf( "skew      (0x%x): 0x%02x\n", REG_SKEW, regs->skew);
	printf( "--------------------\n");

	for ( int i = 0; i < 16; i++ )
		printf ( "%X  ", BlitterHalftone [i] );

	printf ( "\n" );
	printf( "--------------------\n");
}