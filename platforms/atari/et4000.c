/*
 * PiStorm Atari
 * Steve Bradford aka cryptodad
 *
 * ET4000 emulation
 * 
 * Modes
 * 4,5 320x200 2bpp 4 colours
 * 6   640x200 1bpp 2 colours
 * D   320x200 4bpp 16 colours
 * E   640x200 4bpp
 * 10  640x350
 * 12  640x480 4bpp
 * 
 * plane 0 = Blue
 * plane 1 = Green
 * plane 2 = Red
 * plane 3 = Intensity
 * 
 * NOTES
 * EMUtos uses 8bit IO
 * Max resolution is 1024x768 8bpp (256 colours)
 * If using 256 colours or more, linear byte system is used otherwise plane system is used
 * 
 */

#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include "../../config_file/config_file.h"
#include <stdbool.h>
#include "et4000.h"


//#define TRYDMA
#ifdef TRYDMA
#include <sys/ioctl.h>
#include <sys/mman.h>

#define BCM2708_PERI_BASE	0xFE000000  // pi4
#define DMA_BASE        (BCM2708_PERI_BASE + 0x007000)

// DMA control block (must be 32-byte aligned)
typedef struct 
{
    uint32_t ti,    // Transfer info
        srce_ad,    // Source address
        dest_ad,    // Destination address
        tfr_len,    // Transfer length
        stride,     // Transfer stride
        next_cb,    // Next control block
        debug,      // Debug register
        unused;
} 
DMA_CB __attribute__ ( (aligned(32)) );

#define DMA_CB_DEST_INC (1<<4)
#define DMA_CB_SRC_INC  (1<<8)

// DMA register definitions
#define DMA_CHAN        5
//#define DMA_PWM_DREQ    5
#define DMA_CS          (DMA_CHAN*0x100)
#define DMA_CONBLK_AD   (DMA_CHAN*0x100 + 0x04)
#define DMA_TI          (DMA_CHAN*0x100 + 0x08)
#define DMA_SRCE_AD     (DMA_CHAN*0x100 + 0x0c)
#define DMA_DEST_AD     (DMA_CHAN*0x100 + 0x10)
#define DMA_TXFR_LEN    (DMA_CHAN*0x100 + 0x14)
#define DMA_STRIDE      (DMA_CHAN*0x100 + 0x18)
#define DMA_NEXTCONBK   (DMA_CHAN*0x100 + 0x1c)
#define DMA_DEBUG       (DMA_CHAN*0x100 + 0x20)
#define DMA_ENABLE      0xff0

#define BUS_TO_PHYS(x) ((x) & ~0xC0000000)

#define VIRT_DMA_REG(a) ((volatile uint32_t *)((uint32_t)virt_dma_regs + a))

extern volatile unsigned int *gpio;
void *bus_dma_mem;

// Virtual memory for DMA descriptors and data buffers (uncached)
void *virt_dma_mem;

// Convert virtual DMA data address to a bus address
#define BUS_DMA_MEM(a)  ((uint32_t)a-(uint32_t)virt_dma_mem+(uint32_t)bus_dma_mem)

//char *dma_regstrs[] = {"DMA CS", "CB_AD", "TI", "SRCE_AD", "DEST_AD",
//    "TFR_LEN", "STRIDE", "NEXT_CB", "DEBUG", ""};
volatile void *virt_dma_regs;

// ----- DMA -----

// Enable and reset DMA
void enable_dma(void)
{
    *VIRT_DMA_REG(DMA_ENABLE) |= (1 << DMA_CHAN);
    *VIRT_DMA_REG(DMA_CS) = 1 << 31;
}

// Start DMA, given first control block
void start_dma(DMA_CB *cbp)
{
    *VIRT_DMA_REG(DMA_CONBLK_AD) = BUS_DMA_MEM(cbp);
    *VIRT_DMA_REG(DMA_CS) = 2;       // Clear 'end' flag
    *VIRT_DMA_REG(DMA_DEBUG) = 7;    // Clear error bits
    *VIRT_DMA_REG(DMA_CS) = 1;       // Start DMA
}

// Halt current DMA operation by resetting controller
void stop_dma(void)
{
    //if (virt_dma_regs)
        *VIRT_DMA_REG(DMA_CS) = 1 << 31;
}
#endif


extern int FRAME_RATE;
extern int fbfd;
extern void *fbp;
extern void *fbptr;
extern struct fb_var_screeninfo vinfo;
extern struct fb_fix_screeninfo finfo;
extern size_t screensize;
extern void cpu2 ( void );
extern volatile int cpu_emulation_running;
extern volatile uint32_t RTG_VSYNC;
extern void *RTGbuffer;
extern void *VRAMbuffer;
extern volatile int RTGresChanged;
extern volatile int g_buserr;
extern bool RTG_EMUTOS_VGA;
extern pthread_mutex_t rtglock;

static bool first;
static int ix;

volatile nova_xcb_t nova_xcb;
volatile nova_xcb_t *xcb;
volatile bool ET4000enabled;
volatile int COLOURDEPTH;
volatile uint32_t RTG_VSYNC;
volatile int RTGresChanged;

bool screenGrab = false;
bool ET4000Initialised;
//int    windowWidth;
//int    windowHeight;

extern int kbhit (void);
extern void screenDump (int, int);

const uint32_t vga_palette[VGA_PALETTE_LENGTH] = 
{
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



int et4000Init ( void )
{
    xcb = &nova_xcb;

    xcb->mode = 2; /* VGA */
    xcb->VGAmode = 0x80; /* default to VGA mode as opposed to EGA */
    xcb->videoSubsystemr = 0x00; /* configure port address 0x3c3 */
    //xcb->videoSubsystemr = 0x08; /* configure port address 0x46e8 */
    xcb->atc_ixdff = false;
    xcb->atc_ix = 0;
    xcb->KEY = false;
    xcb->KEYenable = false;
    xcb->ISr0 = 0;
    xcb->FCr = 0;
    xcb->ts_index [0] = 0x03;
    xcb->ts_index [3] = 0x00;
    xcb->ts_index [6] = 0x00;
    xcb->ts_index [7] = 0xBC;
    
    first = true;
    ET4000enabled = false;
    ET4000Initialised = true;
    RTG_VSYNC = 1;

    return 1;
}


static void     *dst;
static void     *src;    
static uint8_t  plane0, plane1, plane2, plane3;
static uint32_t address;
static uint32_t pixel;
static int      SCREEN_SIZE;
static struct   timeval stop, start;


void et4000Draw ( int windowWidth, int windowHeight )
//void et4000Draw ( uint32_t offset )
{
    src = (void *)NULL; 
    dst = (void *)NULL;
    SCREEN_SIZE = windowWidth * windowHeight;


    //RTG_VSYNC = 0;
    //pthread_mutex_lock ( &rtglock ); 

    //memcpy ( RTGbuffer, VRAMbuffer, MAX_VRAM );

    /* Monochrome */
    if ( COLOURDEPTH == 1 )
    {
     
        //if ( !fbptr )
        //    return;
        uint16_t *dptr = RTGbuffer;//fbptr;
        uint8_t  *sptr = VRAMbuffer;//RTGbuffer;
        //int ppb;

        for ( uint32_t address = 0, pixel = 0; pixel < SCREEN_SIZE; address++ ) 
        //uint32_t address = offset;
        //uint32_t pixel = 0;
        //int ppb = 0;
        {
            //pthread_mutex_lock ( &rtglock ); 

            for ( int ppb = 0; ppb < 8; ppb++, pixel++ )
            {
                dptr [pixel] = ( sptr [address] >> (7 - ppb) ) & 0x1 ? 0x0020 : 0xffff;
            }

            //pthread_mutex_unlock ( &rtglock ); 
        }

       // memcpy ( fbp, VRAMbuffer, SCREEN_SIZE );
    }
#if (1)
    /* Colour 8bit 256 colours */
    else if ( COLOURDEPTH == 2 )
    {
        uint16_t *dptr = RTGbuffer;//fbptr;
        uint8_t  *sptr = VRAMbuffer;//RTGbuffer;
        int      ix;
        uint8_t  r, g, b;
        int      x;
        int      y;

        //pthread_mutex_lock ( &rtglock ); 

        for ( address = 0, pixel = 0; pixel < SCREEN_SIZE; pixel++, address++ ) 
        {
            ix = sptr [address] * 3;                // pointer to palette index
            
            r  = xcb->user_palette [ix] >> 1;     // 6 bit red to 5 bit
            g  = xcb->user_palette [ix + 1];          // 6 bit green 
            b  = xcb->user_palette [ix + 2] >> 1;       // 6 bit blue t 5 bit

            dptr [ pixel ] = r << 11 | g << 5 | b;
        }

        //pthread_mutex_unlock ( &rtglock ); 
    }

    /* Colour 4bit 16 colours */
    /* TODO - cryptodad - IS THIS REALLY NEEDED??? WHO WANTS 16 colours when >= 256 is available */
    else if ( COLOURDEPTH == 3 )
    {
        uint16_t *dptr = RTGbuffer;//fbptr;
        uint8_t *sptr = VRAMbuffer;//RTGbuffer;
        uint32_t colour;
        uint8_t r, g, b;
        int plane0, plane1, plane2, plane3;
        int x;
        int y;
        int zoomX, zoomY;
        int X_SCALE = 1;
        int Y_SCALE = 1;
        

        for ( uint32_t address = 0, pixel = 0; pixel < SCREEN_SIZE; address += 1 ) 
        //for ( uint32_t address = 0, pixel = 0; address < SCREEN_SIZE; address += 4 ) 
        {
            //for ( int ppb = 0; ppb < 4; ppb++ )
            {
#if (0)
                plane0 = xcb->ts_index [2] & 0x01 ? (  ( sptr [address] ) >> ( 7 - ppb ) ) & 0x1 : 0; // Blue      
                plane1 = xcb->ts_index [2] & 0x02 ? (  ( sptr [address + 0x10000] ) >> ( 7 - ppb ) ) & 0x1 : 0; // Green     
                plane2 = xcb->ts_index [2] & 0x04 ? (  ( sptr [address + 0x20000] ) >> ( 7 - ppb ) ) & 0x1 : 0; // Red       
                plane3 = xcb->ts_index [2] & 0x08 ? (  ( sptr [address + 0x30000] ) >> ( 7 - ppb ) ) & 0x1 : 0; // Intensity 

                colour = vga_palette [plane3 << 3 | plane2 << 2 | plane1 << 1 | plane0];

                r = ((colour >> 16) & 0xff);
                g = ((colour >> 8) & 0xff);
                b = (colour & 0xff);
                //printf ( "colour = 0x%X\n", colour );
#else
                //plane0 = ( ( sptr [address    ] ) >> ( 7 - ppb ) ) & 0x1; // Blue      
                //plane1 = ( ( sptr [address + 1] ) >> ( 7 - ppb ) ) & 0x1; // Green     
                //plane2 = ( ( sptr [address + 2] ) >> ( 7 - ppb ) ) & 0x1; // Red       
                //plane3 = ( ( sptr [address + 3] ) >> ( 7 - ppb ) ) & 0x1; // Intensity 

                //colour = vga_palette [plane3 << 3 | plane2 << 2 | plane1 << 1 | plane0];
                colour = vga_palette [ sptr [address] & 0x0F ];
               
                r = ((colour >> 16) & 0xff) >> 3;
                g = ((colour >> 8) & 0xff) >> 2;
                b = (colour & 0xff) >> 3;

                dptr [pixel++] = r << 11 | g << 5 | b;
#endif
                //x = pixel % windowWidth;
               // y = pixel / windowWidth;
                //zoomY  = y * Y_SCALE;
                //zoomX  = x * X_SCALE;
                //dptr [ zoomX + (zoomY * windowWidth) ] = r << 11 | g << 5 | b;
                //dptr [pixel] = colour;// r << 11 | g << 5 | b;
                //dptr [pixel++] = r << 11 | g << 5 | b;
            }
        }
    }
    
    /* Colour 16bit 32K/64K colours */
    else if ( COLOURDEPTH == 4 )
    {
        uint16_t *dptr = RTGbuffer;//fbptr;
        uint16_t *sptr = VRAMbuffer;//RTGbuffer;
        
        //pthread_mutex_lock ( &rtglock ); 

        for ( pixel = 0; pixel < SCREEN_SIZE; pixel++ )
        {
            dptr [pixel] = sptr [pixel];
        }

        //pthread_mutex_unlock ( &rtglock ); 
    }

    /* Colour 24bit 16M colours */
    else if ( COLOURDEPTH == 5 )
    {
        uint32_t *dptr = (uint32_t *)RTGbuffer;//fbptr;
        uint8_t *sptr = VRAMbuffer;//RTGbuffer;
        
       // pthread_mutex_lock ( &rtglock ); 

        for ( address = 0, pixel = 0; pixel < SCREEN_SIZE; ) 
        {
            dptr [pixel++] = (uint32_t)( sptr [address++] << 16 | sptr [address++] << 8 | sptr [address++] );
        }

        //pthread_mutex_unlock ( &rtglock ); 
    }
#endif
    
    if ( fbp != (void *)NULL )
    {
        //pthread_mutex_lock ( &rtglock ); 
        memcpy ( fbp, RTGbuffer, screensize );
        //pthread_mutex_unlock ( &rtglock ); 
    }

    //pthread_mutex_unlock ( &rtglock ); 

    //memset ( fbp, 0x8F, screensize );

    //static int i = 0;
    //if ( i % 10 == 0 )
    //  printf ( "still running %d\n", i );

    //i++;
}



void *et4000Render ( void* vptr ) 
{
    static int    wait;
    static int    took;
    static int    windowWidth;
    static int    windowHeight;
    static struct timeval stop, start;
    static bool   unknown = false;
    const struct sched_param priority = {99};


    sched_setscheduler ( 0, SCHED_FIFO, &priority );
   // mlockall ( MCL_CURRENT );  // lock in memory to keep us from paging out

    while ( !cpu_emulation_running )
        ;

    while ( cpu_emulation_running )
    {
        //pthread_mutex_lock ( &rtgmutex );
        /* get time and wait for end-of-frame */
        /* 50 Hz 20000 (20ms), 60 Hz 16666 (16.6ms), 70 Hz 14285 (14.2ms) */
        gettimeofday ( &start, NULL );
        unknown = false;

        if ( ET4000enabled && RTGresChanged )
        {
            /* VGA mode enabled */
            if ( xcb->VGAmode )
            {
                int et4000Res = GETRES ();
              
                if ( et4000Res == 429 )
                {
                    /* NVDI 640x400 Monochrome 71 Hz, 30.0 KHz */
                    windowWidth = 640;
                    windowHeight = 400;
                    
                    if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                            && xcb->ts_index [WRITE_PLANE_MASK] == 0x00
                            && xcb->ts_index [MEMORY_MODE] == 0x00
                            && xcb->MISC_Wr == 0xA3 )
                        COLOURDEPTH = 1;

                    else 
                        unknown = true;
                }

                else if ( et4000Res == 447 )
                {
                    windowWidth = 640;
                    windowHeight = 400;
                    COLOURDEPTH = 1;
                }  

                /* NOVA */
                else if ( et4000Res == 523 )
                {
                    windowWidth = 640;
                    windowHeight = 480;

                    /* Monochrome */
                    if ( xcb->ts_index [TS_AUX_MODE] == 0xF4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x01
                                && xcb->ts_index [MEMORY_MODE] == 0x06 )
                        COLOURDEPTH = 1;

                    /* 8 bit 256 colour */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xF4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F
                                && xcb->ts_index [MEMORY_MODE] == 0x0E
                                && xcb->MISC_Wr == 0xE3 )
                        COLOURDEPTH = 2;

                    /* 4 bit 16 colour */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xF4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F
                                && xcb->ts_index [MEMORY_MODE] == 0x06 )
                        COLOURDEPTH = 3;

                    /* 16 bit 32K/64K */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xB4
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F
                                && xcb->ts_index [MEMORY_MODE] == 0x0E 
                                && xcb->MISC_Wr == 0xE3 )
                        COLOURDEPTH = 4;

                    /* 24 bit true-colour */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F 
                                && xcb->ts_index [MEMORY_MODE] == 0x0E
                                && xcb->MISC_Wr == 0xEF )
                    {
                        printf ( "24 bit\n" );
                        COLOURDEPTH = 5;
                    }

                    else
                    {
                        unknown = true;
                    }
                }

                /* NOVA */
                else if ( et4000Res == 626 )
                {
                    windowWidth = 800;
                    windowHeight = 600;

                    /* 16 bit 32K/64K */
                    if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F )
                        COLOURDEPTH = 4;

                    /* 4 bit 16 colour */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xF4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F 
                                && xcb->ts_index [MEMORY_MODE] == 0x06 )
                        COLOURDEPTH = 3;

                    /* 8 bit 256 colour */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xF4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F
                                && xcb->ts_index [MEMORY_MODE] == 0x0E
                                && xcb->MISC_Wr == 0x23 )
                        COLOURDEPTH = 2;

                    /* Monochrome */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xF4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x01 )
                        COLOURDEPTH = 1;
                }               

                /* NOVA */
                else if ( et4000Res == 803 )
                {
                    windowWidth = 1024;
                    windowHeight = 768;

                    /* 4 bit - 16 colour */
                    if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F
                                && xcb->ts_index [MEMORY_MODE] == 0x06 )
                        COLOURDEPTH = 3;

                    /* 8 bit 256 colour */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F
                                && xcb->ts_index [MEMORY_MODE] == 0x0E )
                        COLOURDEPTH = 2;

                    /* Monochrome */
                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x01 )
                        COLOURDEPTH = 1;
                }

                /* NVDI 640x480 68 Hz, 34.0 KHz */
                else if ( et4000Res == 503 )
                {
                    windowWidth = 640;
                    windowHeight = 480;

                    if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x00
                                && xcb->ts_index [MEMORY_MODE] == 0x00
                                && xcb->MISC_Wr == 0xE7  )
                        COLOURDEPTH = 1;

                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F
                                && (xcb->ts_index [MEMORY_MODE] == 0x00 || xcb->ts_index [MEMORY_MODE] == 0x08)
                                && xcb->MISC_Wr == 0xE7  )
                        COLOURDEPTH = 2;

                    else if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x0F
                                && (xcb->ts_index [MEMORY_MODE] == 0x00 || xcb->ts_index [MEMORY_MODE] == 0x08)
                                && xcb->MISC_Wr == 0xE3  )
                        COLOURDEPTH = 4;

                    else 
                        unknown = true;
                }

                /* NVDI 800x600 60 Hz, 38.0 KHz */
                else if ( et4000Res == 632 )
                {
                    windowWidth = 800;
                    windowHeight = 600;

                    if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x00
                                && xcb->ts_index [MEMORY_MODE] == 0x00
                                && xcb->MISC_Wr == 0xE3  )
                        COLOURDEPTH = 1;

                    else 
                        unknown = true;
                }

                /* NVDI 1024x768 60 Hz, 50.0 KHz */
                else if ( et4000Res == 828 )
                {
                    windowWidth = 1024;
                    windowHeight = 768;

                    if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x00
                                && xcb->ts_index [MEMORY_MODE] == 0x00
                                && xcb->MISC_Wr == 0x2B  )
                        COLOURDEPTH = 1;

                    else 
                        unknown = true;
                }

                /* NVDI 1280x960 50 Hz, 54.0 KHz */
                else if ( et4000Res == 1079 )
                {
                    windowWidth = 1280;
                    windowHeight = 960;

                    if ( xcb->ts_index [TS_AUX_MODE] == 0xB4 
                                && xcb->ts_index [WRITE_PLANE_MASK] == 0x00
                                && xcb->ts_index [MEMORY_MODE] == 0x00
                                && xcb->MISC_Wr == 0xA7  )
                        COLOURDEPTH = 1;
                }


                if ( !unknown )
                {
                    vinfo.xres = windowWidth;
                    vinfo.yres = windowHeight;
                    vinfo.xres_virtual = vinfo.xres;
                    vinfo.yres_virtual = vinfo.yres;
                    vinfo.bits_per_pixel = COLOURDEPTH == 5 ? 32 : 16;

                    if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
                    {
                        unknown = true;
                        
                        //printf ( "ioctl error setting virtual screen\n" );
                        //printf ( "COLOURDEPTH %d, x %d, y %d bpp %d\n", COLOURDEPTH, vinfo.xres, vinfo.yres, vinfo.bits_per_pixel );
                    }

                    else
                    {
                        printf ( "---------------------------\n" );
                        printf ( "TS AUX_MODE = 0x%X\n", xcb->ts_index [TS_AUX_MODE] );
                        printf ( "TS WRITE_PLAN_MASK = 0x%X\n", xcb->ts_index [WRITE_PLANE_MASK] );
                        printf ( "TS MEMORY MODE = 0x%X\n", xcb->ts_index [MEMORY_MODE] );
                        printf ( "MISC_OUTPUT reg = 0x%X\n", xcb->MISC_Wr );                        
                        printf ( "GETRES = %d\n", et4000Res );
                        printf ( "RESOLUTION = %dx%d\n", windowWidth, windowHeight );
                        printf ( "COLOURDEPTH = %s\n", 
                                    COLOURDEPTH == 1 ? "Monochrome" :
                                    COLOURDEPTH == 2 ? "8 bit 256 colours" :
                                    COLOURDEPTH == 3 ? "4 bit 16 colours" :
                                    COLOURDEPTH == 4 ? "16 bit 32K/64K colours" :
                                    "24 bit True-colour" );
                        printf ( "---------------------------\n" );              
                    
                        /* unmap any previous allocations */
                        if ( fbp )
                        {
                            munmap ( (void *)fbp, screensize );
                            fbp = (void *)NULL;
                            fbptr = (void *)NULL;
                        }

                        /* Get fixed screen information */
                        if ( ioctl ( fbfd, FBIOGET_FSCREENINFO, &finfo ) ) 
                        {
                            printf ( "ioctl error getting screen info\n" );
                        }

                        else
                        {
                            screensize = finfo.smem_len; 

                            fbp = mmap ( 0, 
                                        screensize, 
                                        PROT_READ | PROT_WRITE, 
                                        MAP_SHARED, 
                                        fbfd, 
                                        0 );
        
                            fbptr = fbp;    

                            /* clear frame-buffer, set to BLACK */
                            memset ( fbp, 0x00, screensize );

                            RTGresChanged = 0; 
                            //printf ( "res changed - screensize 0x%X\n", screensize );
                        }    
                    }
                }

                else 
                {
                    printf ( "error - unknown resolution - GETRES = %d\n", et4000Res );
                    printf ( "TS AUX_MODE = 0x%X\n", xcb->ts_index [TS_AUX_MODE] );
                    printf ( "TS WRITE_PLAN_MASK = 0x%X\n", xcb->ts_index [WRITE_PLANE_MASK] );
                    printf ( "TS MEMORY MODE = 0x%X\n", xcb->ts_index [MEMORY_MODE] );
                    printf ( "MISC_OUTPUT reg = 0x%X\n", xcb->MISC_Wr ); 
                }
            }   

            else
            {
                unknown = true;

                printf ( "rtgRender () unexpected to be here\n" );   
            } 
        }

        RTG_VSYNC = 0;


        /* draw the screen */

        /* only draw screen if VGA sub-system has been enabled */
        if ( !RTGresChanged && !unknown && ET4000enabled )
        {
            et4000Draw ( windowWidth, windowHeight );
        }

        //else
        //{
        //    printf ( "did not draw RTGresChanged = %d, unknown = %d\n", RTGresChanged, unknown );
        //}

        RTG_VSYNC = 1;
        
        gettimeofday ( &stop, NULL );
        took = ( (stop.tv_sec - start.tv_sec) * 1000000 ) + (stop.tv_usec - start.tv_usec);

        /* rough frame rate delay */
        while ( took < FRAME_RATE )
        //while ( VSYNC )
        {
            /* simple screengrab - could be improved A LOT */
            /* pressing 's' on the keyboard executes a system call to grab the framebuffer */
            /* followed by a second system call to ffmpeg which converts the screendump to a .png file */
            
            if ( screenGrab && kbhit () )
            {
                int c = getchar ();

                if ( c == 's' || c == 'S' )
                    screenDump ( windowWidth, windowHeight );
            }
            
            gettimeofday ( &stop, NULL );

            took = ( (stop.tv_sec - start.tv_sec) * 1000000 ) + (stop.tv_usec - start.tv_usec);
        }

        /* debug */
        //if ( took > (FRAME_RATE + 200) )
        //    printf ( "[ET4000] Frame Overrun - Expected %dus, Actual %dus\n", FRAME_RATE, took );
            //printf ( "[ET4000] Frame Overrun\n" );
    }

    /* if we are here then emulation has ended, hopefully from a user interrupt */
    ET4000enabled = false;

    /* free up RTG memory */
    if ( RTGbuffer )
        free ( RTGbuffer );
        //munmap ( RTGbuffer, screensize );//MAX_VRAM );

    close ( fbfd );
}



uint32_t et4000Read ( uint32_t addr, uint32_t *value, int type )
{
    static uint32_t a;
    static uint32_t offset;


    addr &= 0x00FFFFFF;

    //if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGTOP )
    if ( addr >= 0x00D00000 )
    {
        //printf ( "ET4000 reg write 0x%X\n", addr );

        //if ( (addr - NOVA_ET4000_REGBASE) < 0x50 )
        //    a = 0x3B0 + (addr - NOVA_ET4000_REGBASE);
        
        //else
        //a = addr & 0x3FF;
        a = (addr - NOVA_ET4000_REGBASE);

        *value = 1;

        //printf ( "ET4000 reg read 0x%X\n", addr );

        //if ( (addr - NOVA_ET4000_REGBASE) < 0x50 )
        //    a = 0x3B0 + (addr - NOVA_ET4000_REGBASE);
        
        //else
        //a = addr & 0x3FF;

        switch ( a )
        {
            case 0x3b4: /* monochrome */
                break;
            case 0x3b5: /* monochrome */
                break;
            case 0x3ba: /* monochrome */
                break;

            case 0x3c0: /* ATC register - Address/Index */
                *value = xcb->atc_ix;
                //*value = xcb->atc_index [xcb->atc_ix];
                printf ( "ET4000 iw register = 0x%X, value = 0x%X\n", xcb->atc_ix, *value );
                break;
            case 0x3c1:
                break;
            case 0x3c2: /* Input Status Register Zero */
                *value = xcb->ISr0;
                break;
            case 0x3c3: /* Video Subsystem Register */               

                /* stop emutos initialising ET4000 */
                if ( first )
                {
                    first = false;

                    if ( !RTG_EMUTOS_VGA )
                    {
                        g_buserr = 1; /* raise a BERR if not using EMUtos to init ET4000 */
                        *value = 0xff;
                    }
                }

                else
                    *value = xcb->videoSubsystemr;

                //printf ( "addr 0x%X, value 0x%X, berr %d\n", addr, *value, g_buserr );

                break;
            case 0x3c4:
                *value = xcb->ts_ix;
              //  printf ( "read reg 0x%X TS INDEX = 0x%x\n", a, *value );
                break;
            case 0x3c5:
                *value = xcb->ts_index [xcb->ts_ix];
               // printf ( "read reg 0x%X TS REGISTER %d = 0x%x\n", a, xcb->ts_ix, *value );
                break;
            case 0x3c6: /* PEL Mask */
                *value = 0x0f; /* TODO */
                break;
            case 0x3c7:
                break;
            case 0x3c8: /* */
                *value = xcb->palette_ix_rd;
                break;
            case 0x3c9:
                *value = xcb->user_palette [xcb->palette_ix_rd];
                break;
            case 0x3ca: /* Feature Control Register */
                *value = xcb->FCr;
                break;
            case 0x3cc: /* GENERAL register - Miscellaneous Output */
                *value = xcb->MISC_Wr;
                break;            
            case 0x3cd:
                break;
            case 0x3ce:
                *value = xcb->gdc_ix;
                break;
            case 0x3cf:
                *value = xcb->gdc_index [xcb->gdc_ix];
                break;

            case 0x3d4:
                break;
            case 0x3d5:
                break;
            case 0x3da: /* GENERAL register - Input Status Register One */

                *value = (~RTG_VSYNC << 7) | (RTG_VSYNC << 3) | ET4000enabled;
                xcb->atc_ixdff = false; /* reset index/data flip-flop */
                break;

            default:
                printf ( "ET4000 unknown read register - addr 0x%X\n", a );

                return 1;
        }

        return 1;
    }

    else
    {
        offset = addr - NOVA_ET4000_VRAMBASE;

        
        //if ( fbptr )
        //{
        if ( type == OP_TYPE_BYTE )
            *value = *( uint8_t *)( VRAMbuffer + offset );

        else if ( type == OP_TYPE_WORD )
            *value = be16toh ( *( uint16_t *)( VRAMbuffer + offset ) );

        else if ( type == OP_TYPE_LONGWORD )
            *value = be32toh ( *(uint32_t *)( VRAMbuffer + offset ) );

        //printf ( "addr 0x%X, value = 0x%X\n", addr, *value );

        //printf ( "et4000Read () -> type = %d, addr = 0x%X, berr %d\n", type, addr, g_buserr );
        

        return 1;
        //}
    }

    return 0;
}


uint32_t et4000Write ( uint32_t addr, uint32_t value, int type )
{
    static uint32_t offset;
    static uint32_t a;
    static int invalid = 0;

    addr &= 0x00FFFFFF;

   // if ( ((addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_REGTOP) || (addr >= 0xFEC00000 && addr < 0xFED20000)) )
    if ( addr >= 0x00D00000 )
    {
        //printf ( "ET4000 reg write 0x%X\n", addr );

        //if ( (addr - NOVA_ET4000_REGBASE) < 0x50 )
        //    a = 0x3B0 + (addr - NOVA_ET4000_REGBASE);
        
        //else
        //a = addr & 0x3FF;
        a = (addr - NOVA_ET4000_REGBASE);

        switch ( a )
        {

            case 0x3b4:
                break;
            case 0x3b5:
                break;
            case 0x3b8: /* Mode Control Register monochrome */
                break;
            case 0x3bf: /* Hercules Compatibility Register */
                if ( value == 0x03 )
                    xcb->KEYenable = true;
                break;
            case 0x3ba: /* monochrome */
                break;

            case 0x3c0: /* ATC (Attribute Controller) register - Address/Index (23 registers) */
                
                if ( xcb->atc_ixdff == false ) /* index */
                    xcb->atc_ix = value & 0x1f;

                if ( xcb->atc_ixdff == true )   /* data */
                    xcb->atc_index [xcb->atc_ix] = value;

                /* every write toggles the index/data bit */
                xcb->atc_ixdff = !xcb->atc_ixdff;
                //xcb->atc_paletteRAMaccess = value & 0x20;

                break;

            case 0x3c2: /* GENERAL register - Miscellaneous Output Register */
                //printf ( "MISC_W using CRTC addresses %s\n",
                //    value & 0x01 ? "3Dx Colour" : "3Bx Monochrome" );
                //printf ( "MISC_W DMA access %s\n",
                //    value & 0x02 ? "enabled" : "disabled" );
                
                /* ref - Tseng Labs pg32 2.2.2 */
                /* 
                 * cs [1,0] bits (clock select)
                 * 0 = VGA Mode
                 * 1 = VGA mode/CGA
                 * 2 = EGA mode
                 * 3 = Extended mode 
                 * */
                printf ( "MISC_W MCLK = 0x%X\n", value & 0x0c );
                printf ( "MISC_W CS bits = 0x%X\n", value & 0x03 );
                //printf ( "MISC_W all bits = 0x%X\n", value );

                /* Mode of Operation */

                /* EGA MODES */
                /*     0      1       2      3   4  5  6    7   D  E  F  10 */
                /* 63/A3/63 63/A3 63/A3/63 63/A3 63 63 63 A2/62 63 63 A2 A3 */

                /* VGA MODES = EGA MODES + */
                /* 11 12 13 */
                /* E3 E3 63 */

                /* TLI EXTENDED MODES */
                /* 22 23 24 25 26 29 2A 2D 2E 30 37i 37n 2F 38i 38n */
                /* A7 A7 A7 E3 E3 EF EF A3 E3 EF 2F  3F  63 2F  3F  */

                RTGresChanged = 1;   
                xcb->MISC_Wr = value;

                break;
            case 0x3c3:
            case 0x46e8:
                /* enable VGA mode */
                if ( value == 0x01 )
                {
                    ET4000enabled = true;
                    printf ( "ET4000 Enable VGA SubSystem\n" );
                }

                else
                {
                    ET4000enabled = false;
                    printf ( "ET4000 Disable VGA SubSystem 0x%X\n", value );
                }

                xcb->videoSubsystemr = value;
                break;

            /* 
            * 3C6 PALETTE MASK
            * 3C7 PALETTE READ
            * 3C8 PALETTE WRITE
            * 3C9 PALETTE DATA
            */
            case 0x3c4: /* TS Index */
                xcb->ts_ix = value & 0x07;
                break;
            case 0x3c5: /* TS Indexed Register 2: Write Plane Mask */

                //if ( xcb->ts_ix == 2 )
                    //printf ( "TS Write Plane Mask = 0x%X\n", value );

                if ( xcb->ts_ix == 4 )
                {
                    if ( (xcb->ts_index [xcb->ts_ix] & 0x08) != (value & 0x08) )
                    {
                        //printf ( "ET4000 Resolution changed\n" );
                        
                        //RTGresChanged = 1;
                        //RTG_RES = RESOLUTION3; // arbitrary value - will be set later, but needs to be > HI_RES
                    }
                
                    //printf ( "TS Memory Mode = 0x%X - Map mask = %d, %s mode enabled\n", value, value & 0x04, value & 0x08 ? "Chain 4 (Linear)" : "Planer" );
                }

                else if ( xcb->ts_ix == 7 )
                {
                   // printf ( "TS Auxillary Mode = 0x%X - %s mode enabled\n", value, value & 0x80 ? "VGA" : "EGA" );
                    xcb->VGAmode = value & 0x80;
                }

                xcb->ts_index [xcb->ts_ix] = value;

                break;
            
            case 0x3c6:
                xcb->paletteWrite = false;

                if ( value == 0xFF )
                    xcb->paletteWrite = true;
                
                break;
            case 0x3c7: /* PEL READ ADDRESS */
                xcb->palette_ix_rd = value * 3;
                break;
            case 0x3c8:
                //printf ( "palette index = %d\n", value );
                xcb->palette_ix = value * 3;
            
                break;
            case 0x3c9: /* PEL DATA - NOTE must have three consecutive writes to populate RGB */
                //printf ( "palette index 0x%04X = 0x%X\n", xcb->palette_ix, value );
                if ( xcb->paletteWrite == true )
                {
                    xcb->user_palette [xcb->palette_ix] = value;
                    xcb->palette_ix += 1;
                }

                break;

            case 0x3cd: /* GDC Segment Select */

                //if ( xcb->KEY && !(xcb->crtc_index [VSCONF1] & 0x10) )
                //{
                    xcb->gdc_segment_select = value & 0xFF;
                    printf ( "GDC Segment Select 0x%X\n", xcb->gdc_segment_select );
                //}

                break;

            case 0x3ce: /* GDC (Graphics Data Controller) Index register */
                xcb->gdc_ix = value & 0x0f;
                //printf ( "TODO GDC Index 0x%X\n", value );
                break;
            case 0x3cf:

                xcb->gdc_index [xcb->gdc_ix] = value;
                //printf ( "TODO GDC Register 0x%X = 0x%X\n", xcb->gdc_ix, value );

                switch ( xcb->gdc_ix )
                {
                    /* Set/Reset */
                    case 0:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0x0F;
                        break;

                    /* Enable Set/Reset */
                    case 1:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0x0F;
                        break;
                    
                    /* Colour Compare */
                    case 2:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0x0F;
                        break;
                    
                    /* Data Rotate */
                    case 3:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0x0F;
                        break;
                    
                    /* Read Plane Select */
                    case 4:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0x03;
                        break;

                    /* GDC Mode */
                    case 5:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0xFF;

                        //if ( xcb->gdc_index [xcb->gdc_ix] & 0x40 )
                        //    printf ( "GDC Register 5 = 256 colour enabled\n" );

                        break;

                    /* Miscellaneous */
                    case 6:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0x0F;
                        break;

                    /* Colour Care */
                    case 7:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0x0F;
                        break;

                    /* Bit Mask */
                    case 8:
                        xcb->gdc_index [xcb->gdc_ix] = value & 0xFF;
                        break;

                    default:
                        printf ( "GDC Registers - Invalid register %d\n", xcb->gdc_ix );
                        break;
                }
                
                break;
            
            case 0x3d4: /* 6845 CRT Control Register */
                xcb->crtc_ix = value;
                //printf ( "3d4 0x%X\n", value );
                
                break;
            case 0x3d5: /* 6845 CRT Data Register */
                if ( (xcb->crtc_ix < 0x32) || (xcb->crtc_ix == 0x33) || (xcb->crtc_ix == 0x35) || (xcb->crtc_ix > 0x18 && xcb->KEY) )
                    xcb->crtc_index [xcb->crtc_ix] = value;

                //if ( xcb->crtc_ix > 0x35 && xcb->KEY )
                //    printf ( "6845 Video System Configuration %d = 0x%X\n", xcb->crtc_ix == VSCONF1 ? 1 : 2, xcb->crtc_index [xcb->crtc_ix] );

                if ( xcb->crtc_ix == VSCONF1 && xcb->KEY )
                {
                    //printf ( "6845 VSCONF1 Addressing Mode %s\n", value & 0x20 ? "TLI" : "IBM" );
                    printf ( "6845 VSCONF1 16 bit display memory r/w %s\n", value & 0x40 ? "enabled" : "disabled" );
                    printf ( "6845 VSCONF1 16 bit IO r/w %s\n", value & 0x80 ? "enabled" : "disabled" );
                    xcb->VSCONF1r = value;
                }

                if ( xcb->crtc_ix == VSCONF2 && xcb->KEY )
                {
                    printf ( "6845 VSCONF2 Addressing Mode %s\n", value & 0x20 ? "TLI" : "IBM" );
                    xcb->VSCONF2r = value;
                }

                break;
            case 0x3d8: /* (RW) 6845 Display Mode Control Register colour */
                if ( value == 0xA0 && xcb->KEYenable )
                    xcb->KEY = true;

                else
                {
                    xcb->DisplayModeControlr = value;

                    printf ( "DisplayModeControlRegister - %s\n", value & 0x04 ? "Monochrome" : "Colour" );
                    printf ( "DisplayModeControlRegister - %s\n", value & 0x10 ? "Monochrome 640x200" : "" );
                    
                    printf ( "Display Mode Control Register 3D8 = 0x%X\n", value );
                }
                break;
            case 0x3d9: /* (WO) 6845 Display Colour Control Register */
                printf ( "Colour Select Register - 0x%X\n", value );
                break;
            case 0x3da: /* 6845 Display Status Control Register */
                break;

            default:
                printf ( "ET4000 unknown write register - addr = 0x%X\n", a );

                return 1;
        }

        return 1;
    }

    /* should then be a VRAM address */
    
    else
    {
       // printf ( "et4000Write () -> type = %d, addr = 0x%X\n", type, addr );
     
        offset = addr - NOVA_ET4000_VRAMBASE;

        //if ( !RTGresChanged )
        //{
            //memset ( VRAMbuffer, 0xFF, MAX_VRAM );
        //}

        //if ( xcb->gdc_index [6] & 0x01 )
        //else
       // {
            if ( type == OP_TYPE_BYTE )
            {
                *( (uint8_t *)( VRAMbuffer + offset ) ) = value;// & xcb->gdc_index [8];
             
            }

            else if ( type == OP_TYPE_WORD )
            {
                *( (uint16_t *)( VRAMbuffer + offset) ) = htobe16 (value);
              
            }

            else if ( type == OP_TYPE_LONGWORD )
            {
                *( (uint32_t *)( VRAMbuffer + offset ) ) = htobe32 (value);
                
            }
        //}
      
        //COLOURDEPTH = 1;
        //et4000Draw ( offset );

        return 1;
    }

    return 0;
}
