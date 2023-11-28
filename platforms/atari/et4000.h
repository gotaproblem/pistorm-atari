#ifndef ET4000_H
#define ET4000_H

#define NOVA_ET4000_REGBASE  0x00D00000
#define NOVA_ET4000_REGTOP   0x00D00400
#define NOVA_ET4000_VRAMBASE 0x00C00000
#define NOVA_ET4000_VRAMBASE 0x00C00000
#define NOVA_ET4000_VRAMSIZE 0x00100000
#define NOVA_ET4000_VRAMTOP  NOVA_ET4000_REGBASE



/* complements of EMUtos */
#define EXT_REG 0x3BF   /* extended registers ??? */

#define CRTC_I  0x3D4   /* CRT Controller index and data ports */
#define CRTC_D  0x3D5
#define GDC_SEG 0x3CD   /* GDC segment select, index and data ports */
#define GDC_I   0x3CE
#define GDC_D   0x3CF
#define TS_I    0x3C4   /* Timing Sequencer index and data ports */
#define TS_D    0x3C5
#define VIDSUB  0x3C3   /* Video Subsystem register */
#define MISC_W  0x3C2   /* Misc Output Write Register */
#define DAC_PEL 0x3C6   /* RAMDAC pixel mask */
#define DAC_IW  0x3C8   /* RAMDAC write index */
#define DAC_D   0x3C9   /* RAMDAC palette data */
#define ATC_IW  0x3C0   /* Attribute controller: index and data write */
#define IS1_RC  0x3DA   /* Input Status Register 1: color emulation */


/* Timing Sequence Registers */
#define SYNC_RESET			0
#define TS_MODE				1
#define WRITE_PLANE_MASK	2
#define FONT_SELECT			3
#define MEMORY_MODE			4
#define TS_STAT				6
#define TS_AUX_MODE			7

/*--- Types ---*/

typedef struct {
	unsigned char	name[33];	/* Video mode name */
	unsigned char	dummy1;

	unsigned short	mode;		/* Video mode type */
								/*  0=4 bpp */
								/*  1=1 bpp */
								/*  2=8 bpp */
								/*  3=15 bpp (little endian) */
								/*  4=16 bpp (little endian) */
								/*  5=24 bpp (BGR) */
								/*  6=32 bpp (RGBA) */
	unsigned short	pitch;		/* bpp<8: words/plane /line */
								/*  bpp>=8: bytes /line */
	unsigned short	planes;		/* Bits per pixel */
	unsigned short	colors;		/* Number of colours */
	unsigned short	hc_mode;	/* Hardcopy mode */
								/*  0=1 pixel screen -> 1x1 printer screen */
								/*  1=1 pixel screen -> 2x2 printer screen */
								/*  2=1 pixel screen -> 4x4 printer screen */
	unsigned short	max_x;		/* Max x,y coordinates, values-1 */
	unsigned short	max_y;
	unsigned short	real_x;		/* Real max x,y coordinates, values-1 */
	unsigned short	real_y;

	unsigned short	freq;		/* Pixel clock */
	unsigned char	freq2;		/* Another pixel clock */
	unsigned char	low_res;	/* Half of pixel clock */
	unsigned char	r_3c2;
	unsigned char	r_3d4[25];
	unsigned char	extended[3];
	unsigned char	dummy2;
} nova_resolution_t;

#define VSCONF1 0x36
#define VSCONF2 0x37
#define VGA     1
#define EGA     0

/* cookie NOVA points to this */
typedef struct 
{
    uint8_t videoSubsystemr;
    uint8_t VGAmode;
    bool    atc_ixdff;
    uint8_t atc_ix;
    uint8_t atc_index [23];
    uint8_t atc_data [23];
    bool    atc_paletteRAMaccess;
    uint8_t gdc_ix;
    uint8_t gdc_index [11];
	uint8_t gdc_segment_select;
    bool    KEY;
	bool    KEYenable;
    uint8_t ISr0;
    uint8_t FCr;
    uint8_t DisplayModeControlr;
    uint8_t crtc_index [0x38];
    uint8_t crtc_ix;
    uint8_t ts_ix;
    uint8_t ts_index [8];
    uint8_t VSCONF1r;
    uint8_t VSCONF2r;
    uint8_t MISC_Wr;
    bool    paletteWrite;
    uint8_t user_palette [256*3];
    uint16_t palette_ix;
    uint16_t palette_ix_rd;

	unsigned char	version[4];	/* Version number */
	unsigned char	resolution;	/* Resolution number */
	unsigned char	blnk_time;	/* Time before blanking */
	unsigned char	ms_speed;	/* Mouse speed */
	unsigned char	old_res;

	/* Pointer to routine to change resolution */
	void			(*p_chres)(nova_resolution_t *nova_res, unsigned long offset);
	
	unsigned short	mode;		/* Video mode type: */
								/*  0=4 bpp */
								/*  1=1 bpp */
								/*  2=8 bpp */
								/*  3=15 bpp (little endian) */
								/*  4=16 bpp (little endian) */
								/*  5=24 bpp (BGR) */
								/*  6=32 bpp (RGBA) */
	unsigned short	pitch;		/* bpp<8: bytes per plane, per line */
								/*  bpp>=8: bytes per line */
	unsigned short	planes;		/* Bits per pixel */
	unsigned short	colours;	/* Number of colours, unused */
	unsigned short	hc;			/* Hardcopy mode */
								/*  0=1 pixel screen -> 1x1 printer screen */
								/*  1=1 pixel screen -> 2x2 printer screen */
								/*  2=1 pixel screen -> 4x4 printer screen */
	unsigned short	max_x, max_y;		/* Resolution, values-1 */
	unsigned short	rmn_x, rmx_x;
	unsigned short	rmn_y, rmx_y;
	unsigned short	v_top, v_bottom;
	unsigned short	v_left, v_right;

	/* Pointer to routine to set colours */
	void			(*p_setcol)(unsigned short index, unsigned char *colors);	

	void			(*chng_vrt)(unsigned short x, unsigned short y);
	void			(*inst_xbios)(unsigned short on);
	void			(*pic_on)(unsigned short on);
	void			(*chng_pos)(nova_resolution_t *nova_res, unsigned short direction, unsigned short offset);
	void			(*p_setscr)(void *adr);	/* Pointer to routine to change screen address */
	void			*base;		/* Address of screen #0 in video RAM */
	void			*scr_base;	/* Adress of video RAM */
	unsigned short	scrn_cnt;	/* Number of possible screens in video RAM */
	unsigned long	scrn_sze;	/* Size of a screen */
	void			*reg_base;	/* Video card I/O registers base */
	void			(*p_vsync)(void);	/* Pointer to routine to vsync */
	unsigned char	name[36];	/* Video mode name */
	unsigned long	mem_size;	/* Global size of video memory */
} volatile nova_xcb_t;

enum { VGA_PALETTE_LENGTH = 256 };
/*
const uint32_t vga_palette[VGA_PALETTE_LENGTH] = {
    0x000000, 0x0002aa, 0x14aa00, 0x00aaaa, 0xaa0003, 0xaa00aa, 0xaa5500, 0xaaaaaa,
    0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xfd55ff, 0xffff55, 0xffffff,
    0x000000, 0x101010, 0x202020, 0x353535, 0x454545, 0x555555, 0x656565, 0x757575,
    0x8a8a8a, 0x9a9a9a, 0xaaaaaa, 0xbababa, 0xcacaca, 0xdfdfdf, 0xefefef, 0xffffff,
    0x0004ff, 0x4104ff, 0x8203ff, 0xbe02ff, 0xfd00ff, 0xfe00be, 0xff0082, 0xff0041,
    0xff0008, 0xff4105, 0xff8200, 0xffbe00, 0xffff00, 0xbeff00, 0x82ff00, 0x41ff01,
    0x24ff00, 0x22ff42, 0x1dff82, 0x12ffbe, 0x00ffff, 0x00beff, 0x0182ff, 0x0041ff,
    0x8282ff, 0x9e82ff, 0xbe82ff, 0xdf82ff, 0xfd82ff, 0xfe82df, 0xff82be, 0xff829e,
    0xff8282, 0xff9e82, 0xffbe82, 0xffdf82, 0xffff82, 0xdfff82, 0xbeff82, 0x9eff82,
    0x82ff82, 0x82ff9e, 0x82ffbe, 0x82ffdf, 0x82ffff, 0x82dfff, 0x82beff, 0x829eff,
    0xbabaff, 0xcabaff, 0xdfbaff, 0xefbaff, 0xfebaff, 0xfebaef, 0xffbadf, 0xffbaca,
    0xffbaba, 0xffcaba, 0xffdfba, 0xffefba, 0xffffba, 0xefffba, 0xdfffba, 0xcaffbb,
    0xbaffba, 0xbaffca, 0xbaffdf, 0xbaffef, 0xbaffff, 0xbaefff, 0xbadfff, 0xbacaff,
    0x010171, 0x1c0171, 0x390171, 0x550071, 0x710071, 0x710055, 0x710039, 0x71001c,
    0x710001, 0x711c01, 0x713900, 0x715500, 0x717100, 0x557100, 0x397100, 0x1c7100,
    0x097100, 0x09711c, 0x067139, 0x037155, 0x007171, 0x005571, 0x003971, 0x001c71,
    0x393971, 0x453971, 0x553971, 0x613971, 0x713971, 0x713961, 0x713955, 0x713945,
    0x713939, 0x714539, 0x715539, 0x716139, 0x717139, 0x617139, 0x557139, 0x45713a,
    0x397139, 0x397145, 0x397155, 0x397161, 0x397171, 0x396171, 0x395571, 0x394572,
    0x515171, 0x595171, 0x615171, 0x695171, 0x715171, 0x715169, 0x715161, 0x715159,
    0x715151, 0x715951, 0x716151, 0x716951, 0x717151, 0x697151, 0x617151, 0x597151,
    0x517151, 0x51715a, 0x517161, 0x517169, 0x517171, 0x516971, 0x516171, 0x515971,
    0x000042, 0x110041, 0x200041, 0x310041, 0x410041, 0x410032, 0x410020, 0x410010,
    0x410000, 0x411000, 0x412000, 0x413100, 0x414100, 0x314100, 0x204100, 0x104100,
    0x034100, 0x034110, 0x024120, 0x014131, 0x004141, 0x003141, 0x002041, 0x001041,
    0x202041, 0x282041, 0x312041, 0x392041, 0x412041, 0x412039, 0x412031, 0x412028,
    0x412020, 0x412820, 0x413120, 0x413921, 0x414120, 0x394120, 0x314120, 0x284120,
    0x204120, 0x204128, 0x204131, 0x204139, 0x204141, 0x203941, 0x203141, 0x202841,
    0x2d2d41, 0x312d41, 0x352d41, 0x3d2d41, 0x412d41, 0x412d3d, 0x412d35, 0x412d31,
    0x412d2d, 0x41312d, 0x41352d, 0x413d2d, 0x41412d, 0x3d412d, 0x35412d, 0x31412d,
    0x2d412d, 0x2d4131, 0x2d4135, 0x2d413d, 0x2d4141, 0x2d3d41, 0x2d3541, 0x2d3141,
    0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000,
};
*/
/*
const uint32_t vga_palette[VGA_PALETTE_LENGTH] = {
  0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA, 0x555555, 0x5555FF
, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF, 0x000000, 0x101010, 0x202020, 0x353535
, 0x454545, 0x555555, 0x656565, 0x757575, 0x8A8A8A, 0x9A9A9A, 0xAAAAAA, 0xBABABA, 0xCACACA, 0xDFDFDF
, 0xEFEFEF, 0xFFFFFF, 0x0000FF, 0x4100FF, 0x8200FF, 0xBE00FF, 0xFF00FF, 0xFF00BE, 0xFF0082, 0xFF0041
, 0xFF0000, 0xFF4100, 0xFF8200, 0xFFBE00, 0xFFFF00, 0xBEFF00, 0x82FF00, 0x41FF00, 0x00FF00, 0x00FF41
, 0x00FF82, 0x00FFBE, 0x00FFFF, 0x00BEFF, 0x0082FF, 0x0041FF, 0x8282FF, 0x9E82FF, 0xBE82FF, 0xDF82FF
, 0xFF82FF, 0xFF82DF, 0xFF82BE, 0xFF829E, 0xFF8282, 0xFF9E82, 0xFFBE82, 0xFFDF82, 0xFFFF82, 0xDFFF82
, 0xBEFF82, 0x9EFF82, 0x82FF82, 0x82FF9E, 0x82FFBE, 0x82FFDF, 0x82FFFF, 0x82DFFF, 0x82BEFF, 0x829EFF
, 0xBABAFF, 0xCABAFF, 0xDFBAFF, 0xEFBAFF, 0xFFBAFF, 0xFFBAEF, 0xFFBADF, 0xFFBACA, 0xFFBABA, 0xFFCABA
, 0xFFDFBA, 0xFFEFBA, 0xFFFFBA, 0xEFFFBA, 0xDFFFBA, 0xCAFFBA, 0xBAFFBA, 0xBAFFCA, 0xBAFFDF, 0xBAFFEF
, 0xBAFFFF, 0xBAEFFF, 0xBADFFF, 0xBACAFF, 0x000071, 0x1C0071, 0x390071, 0x550071, 0x710071, 0x710055
, 0x710039, 0x71001C, 0x710000, 0x711C00, 0x713900, 0x715500, 0x717100, 0x557100, 0x397100, 0x1C7100
, 0x007100, 0x00711C, 0x007139, 0x007155, 0x007171, 0x005571, 0x003971, 0x001C71, 0x393971, 0x453971
, 0x553971, 0x613971, 0x713971, 0x713961, 0x713955, 0x713945, 0x713939, 0x714539, 0x715539, 0x716139
, 0x717139, 0x617139, 0x557139, 0x457139, 0x397139, 0x397145, 0x397155, 0x397161, 0x397171, 0x396171
, 0x395571, 0x394571, 0x515171, 0x595171, 0x615171, 0x695171, 0x715171, 0x715169, 0x715161, 0x715159
, 0x715151, 0x715951, 0x716151, 0x716951, 0x717151, 0x697151, 0x617151, 0x597151, 0x517151, 0x517159
, 0x517161, 0x517169, 0x517171, 0x516971, 0x516171, 0x515971, 0x000041, 0x100041, 0x200041, 0x310041
, 0x410041, 0x410031, 0x410020, 0x410010, 0x410000, 0x411000, 0x412000, 0x413100, 0x414100, 0x314100
, 0x204100, 0x104100, 0x004100, 0x004110, 0x004120, 0x004131, 0x004141, 0x003141, 0x002041, 0x001041
, 0x202041, 0x282041, 0x312041, 0x392041, 0x412041, 0x412039, 0x412031, 0x412028, 0x412020, 0x412820
, 0x413120, 0x413920, 0x414120, 0x394120, 0x314120, 0x284120, 0x204120, 0x204128, 0x204131, 0x204139
, 0x204141, 0x203941, 0x203141, 0x202841, 0x2D2D41, 0x312D41, 0x352D41, 0x3D2D41, 0x412D41, 0x412D3D
, 0x412D35, 0x412D31, 0x412D2D, 0x41312D, 0x41352D, 0x413D2D, 0x41412D, 0x3D412D, 0x35412D, 0x31412D
, 0x2D412D, 0x2D4131, 0x2D4135, 0x2D413D, 0x2D4141, 0x2D3D41, 0x2D3541, 0x2D3141, 0x000000, 0x000000
, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000, 0x000000
};
*/

extern uint32_t et4000Read ( uint32_t, uint32_t*, int );
extern uint32_t et4000Write ( uint32_t, uint32_t, int );
extern int et4000Init ( void );

#endif