/*
 *
 * RTG ( ReTargetable Graphics )
 * An attempt to utilise Pi HDMI for Atari video output
 * 
 * Version 1 will be a one-for-one (mirror) attempt
 * 
 * Atari variables
 * 0x44c (WORD) - screen resolution 0 = 320x200 (low), 1 = 640x200 (med), 2 = 640x400 (high)
 * 0x44e (LONG) - logical screen base
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


#define MAX_WIDTH  1024
#define MAX_HEIGHT 1024
#define MAX_VRAM   (MAX_WIDTH * MAX_HEIGHT * 2)
#define ATARI_VRAM_BASE   0x003f8000 /* 4MB machine - will get changed at detection */
#define GETRES() (uint16_t)( ((xcb->crtc_index [0x35] & 0x02) >> 1 ) << 10 | ((xcb->crtc_index [7] & 0x20) >> 5) << 9 | (xcb->crtc_index [7] & 0x01) << 8 | xcb->crtc_index [6] )
#define HZ50       20000
#define HZ60       16666

static volatile nova_xcb_t nova_xcb;
static volatile nova_xcb_t *xcb;
static bool first;
volatile bool ET4000enabled = false;

bool ET4000Initialised;
void *RTGbuffer = NULL;
void *VRAMbuffer = NULL;
volatile int COLOURDEPTH = 0;
volatile uint32_t RTG_VSYNC;
volatile uint8_t *screen;
volatile int RTGresChanged;
volatile uint16_t RTG_PAL_MODE; /* 0 = NTSC, 1 = PAL */
volatile uint32_t RTG_ATARI_SCREEN_RAM;
volatile uint16_t RTG_PALETTE_REG [16]; /* palette colours */
int ix;
bool RTG_enabled = false;
bool screenGrab = false;
bool RTG_EMUTOS_VGA;
long RTG_fps = 0;
volatile bool VSYNC;
volatile bool RTG_LOCK;
extern volatile bool PS_LOCK;

extern volatile uint32_t RTG_VRAM_BASE;
extern volatile uint32_t RTG_VRAM_SIZE;
extern struct emulator_config *cfg;
extern volatile int cpu_emulation_running;
extern volatile int g_buserr;
extern volatile int cpu_emulation_running;
extern pthread_mutex_t rtgmutex;

// 'global' variables to store screen info
static int fbfd;
static int rtgfd;
void *fbp = 0;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
size_t screensize = 0;
void *fbptr;

void et4000Draw ( int, int );
int FRAME_RATE;

void logo ( void );

/*
 *  Standard colour palette - taken from ATARI ST INTERNALS - ROM listing
 *
 *  $777, $700, $070, $770      White, red, green, yellow
 *  $007, $707, $077, $555      blue, magenta, cyan, light gray
 *  $333, $733, $373, $773      gray, lt. red, lt. green, lt. yellow 
 *  $337, $737, $377, $000      lt. blue, lt. magenta, lt. cyan, black
 */


/* 4 colour pallete */
enum 
{
    WHITE       = 0xffff,
    RED         = 0xf800,
    GREEN       = 0x07e0,
    YELLOW      = 0xffe0,
    BLUE        = 0x001f,
    MAGENTA     = 0xf81f,
    CYAN        = 0x07ff,
    LT_GREY     = 0x7bcf,
    GREY        = 0x5acb,
    LT_RED      = 0xfacb,
    LT_GREEN    = 0x5feb,
    LT_YELLOW   = 0xffef,
    LT_BLUE     = 0x5fff,
    LT_MAGENTA  = 0xf897,
    LT_CYAN     = 0xdfff,
    BLACK       = 0

} palette;

enum 
{
    LOW_RES     = 0,
    MED_RES,
    HI_RES,
    RESOLUTION1,        /* 320x240   */
    RESOLUTION2,        /* 640x480   */
    RESOLUTION3,        /* 800x600   */
    RESOLUTION4         /* 1024x768  */

} resolutions;

uint16_t palette4 [4] = {
   WHITE, RED, LT_GREEN, BLACK
};

uint16_t palette16 [16] = {
    WHITE, RED, LT_GREEN, YELLOW,
    BLUE, MAGENTA, CYAN, LT_GREY,
    GREY, LT_RED, LT_GREEN, LT_YELLOW,         
    LT_BLUE, LT_MAGENTA, LT_CYAN, BLACK };
       
/*
RGB 565 format
red = (unsigned short)((pixel & 0xF800) >> 11); 
green = (unsigned short)((pixel & 0x07E0) >> 5); 
blue = (unsigned short)(pixel & 0x001F);
*/


/*
 * do the conversion - for testing we are using med-res (640x200) - 4 colours
 * two words define a pixel
 * 
 * Pi 32bpp - aarrggbb
 * Pi 16bpp - 
 * 
 */
void draw ( int res ) 
{
    uint8_t plane0, plane1, plane2, plane3;
    int x, y;
    int zoomX, zoomY;
    uint16_t *drawXY;
    int X_SCALE, Y_SCALE;
    int ABPP;
    uint16_t *startingX;
    volatile uint16_t *sptr = (volatile uint16_t *)screen;
    //volatile uint8_t *sptr = (volatile uint8_t *)screen;
    //int xoffset;
    //int yoffset;
    int SCREEN_SIZE;
    //uint16_t *dstptr;
    int lineLength = finfo.line_length;

    
    if ( res == HI_RES )
    {
        ABPP = 1;
        X_SCALE = 2;
        Y_SCALE = 2;
        SCREEN_SIZE = 640 * 400;
    }

    else if ( res == MED_RES )
    {
        ABPP = 2;
        X_SCALE = 2;
        Y_SCALE = 4;
        SCREEN_SIZE = 640 * 200;
        //dstptr = (uint16_t *)fbptr;
    }

    else if ( res == LOW_RES )
    {
        ABPP = 4;
        X_SCALE = 2;//4;
        Y_SCALE = 2;//2;
        //xoffset = (800 - vinfo.xres_virtual) / 2;
        //yoffset = (600 - vinfo.yres_virtual) / 2;
        SCREEN_SIZE = 320 * 200;
    }

    /* VRAM = 32 KB so divide by 2 for 16 bit WORDS */
    //for ( uint32_t address = 0, pixel = 0; address < VRAM / 2; address += ABPP ) 
    for ( uint32_t address = 0, pixel = 0; pixel < SCREEN_SIZE; address += ABPP ) 
    {
        if ( res == HI_RES )
        {
            y      = pixel / lineLength;
            zoomY  = y * Y_SCALE;
            startingX = fbptr + (pixel % lineLength) * X_SCALE + (zoomY * lineLength);//finfo.line_length);
            
            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                x      = pixel % lineLength;
                y      = pixel / lineLength;
                //zoomY  = y * Y_SCALE;
                zoomX  = x * X_SCALE;
                drawXY = fbptr + zoomX + (zoomY * lineLength);//finfo.line_length); 

                //for ( int X = 0; X < X_SCALE; X++ )
                    *( drawXY + 0 ) = plane0 == 1 ? BLACK : WHITE; 

                pixel++;
            }    

            //if ( Y_SCALE > 1 )
            //    memcpy ( startingX + 1 + finfo.line_length, startingX, 640 * (Y_SCALE - 1) );
        }

        if ( res == MED_RES )
        {
            y      = pixel / 640;
            zoomY  = y * Y_SCALE;
            //startingX = fbptr + (pixel % 640) * X_SCALE + (zoomY * finfo.line_length);
            startingX = fbptr + (pixel % 640) * X_SCALE + (zoomY * 640);

            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                plane1 = ( be16toh ( sptr [ address + 1 ] ) >> ( 15 - n ) ) & 1; 
                x      = pixel % 640;
                zoomX  = x * X_SCALE;
                drawXY = fbptr + zoomX + (zoomY * 640);//finfo.line_length); 

               // for ( int X = 0; X < X_SCALE; X++ )
                    //*( drawXY + X ) = RTG_PALETTE_REG [plane1 << 1 | plane0]; 
                    *drawXY = RTG_PALETTE_REG [plane1 << 1 | plane0]; 
                //dstptr [pixel] = RTG_PALETTE_REG [plane1 << 1 | plane0]; 
                
                pixel++; 
            }

            //if ( Y_SCALE > 1 )
                //memcpy ( startingX + 1 + finfo.line_length, startingX, 640 * (Y_SCALE - 1) );
            //    memcpy ( fbptr + ( (y + 1) * 640), fbptr + (y * 640), 640 );
            
            //printf ( "pixel %d\n", pixel );
        }

        else if ( res == LOW_RES )
        {
            
            y      = (pixel / lineLength);// + yoffset;
            //if ( y > 199 )
            //    printf ( "y too big %d\n", y );
            zoomY  = y * Y_SCALE;
            //printf ( "here 1\n" );
            //startingX = fbptr + (pixel % 320) * X_SCALE + (zoomY * finfo.line_length);
            startingX = fbptr + (pixel % lineLength) * X_SCALE + (zoomY * lineLength);
            //printf ( "here 2\n" );
            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                plane1 = ( be16toh ( sptr [ address + 1 ] ) >> ( 15 - n ) ) & 1; 
                plane2 = ( be16toh ( sptr [ address + 2 ] ) >> ( 15 - n ) ) & 1; 
                plane3 = ( be16toh ( sptr [ address + 3 ] ) >> ( 15 - n ) ) & 1; 
                x      = (pixel % lineLength);// + xoffset;
                //if ( x > 319 )
                //    printf ( "x too big %d\n", x );
                zoomX  = x * X_SCALE;
                drawXY = fbptr + zoomX + (zoomY * lineLength);//finfo.line_length); 
                //printf ( "drawXY = 0x%X\n", drawXY );
                for ( int X = 0; X < X_SCALE; X++ )
                    *( drawXY + X ) = RTG_PALETTE_REG [plane3 << 3 | plane2 << 2 | plane1 << 1 | plane0]; 

                pixel++;
            }  
            //printf ( "here 3\n" );
            //if ( Y_SCALE > 1 )
                //memcpy ( startingX + 1 + finfo.line_length, startingX, 320 * (Y_SCALE - 1) );
                //memcpy ( startingX + 1 + 320, startingX, 320 * (Y_SCALE - 1) );
            //printf ( "here 4\n" );
        }  
    }
}


int kbhit (void);
void screenDump (int, int);


void *rtgRender ( void* vptr ) 
{
    static int    wait;
    static int    took;
    static int    windowWidth;
    static int    windowHeight;
    static struct timeval stop, start;
    static bool   unknown = false;


    while ( !cpu_emulation_running )
        ;

    while ( cpu_emulation_running )
    {
        //pthread_mutex_lock ( &rtgmutex );
        /* get time and wait for end-of-frame */
        /* 50 Hz 20000 (20ms), 60 Hz 16666 (16.6ms), 70 Hz 14285 (14.2ms) */
        gettimeofday ( &start, NULL );
        unknown = false;

        if ( ET4000enabled && RTGresChanged && !unknown )
        {
            COLOURDEPTH = 1;
            
#ifdef NATIVE_RES
            if ( thisRES == LOW_RES )
            {
                vinfo.xres = 320;
                vinfo.yres = 200;
                vinfo.xres_virtual = 320;
                vinfo.yres_virtual = 200;
                vinfo.bits_per_pixel = 16;
                if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ))
                {
                    printf ( "Error setting virtual; width / height\n" );
                }

                else
                {
                    printf ( "[RTG] Resolution change to LOW_RES\n" );

                    memcpy ( (void *)RTG_PALETTE_REG, palette16, sizeof palette16 );
                }

                if ( RTG_PAL_MODE )
                    FRAME_RATE = 20000; 
                
                else
                    FRAME_RATE = 16666; 
            }

            else if ( thisRES == MED_RES )
            {
                vinfo.xres = 640;
                vinfo.yres = 400;
                vinfo.xres_virtual = 640;
                vinfo.yres_virtual = 200;
                vinfo.bits_per_pixel = 16;
                if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ))
                {
                    printf ( "Error setting virtual; width / height\n" );
                }

                else
                {
                    printf ( "[RTG] Resolution change to MED_RES\n" );

                    memcpy ( (void *)RTG_PALETTE_REG, palette4, sizeof palette4 );
                }

                if ( RTG_PAL_MODE )
                    FRAME_RATE = 20000; 
                
                else
                    FRAME_RATE = 16666; 
            }

            else if ( thisRES == HI_RES )
            {
                vinfo.xres = 640;
                vinfo.yres = 400;
                vinfo.xres_virtual = 640;
                vinfo.yres_virtual = 400;
                vinfo.bits_per_pixel = 16;
                if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
                {
                    printf ( "Error setting virtual; width / height\n" );
                }

                else
                    printf ( "[RTG] Resolution change to HI_RES\n" );

                FRAME_RATE = 14285;
            }

            else if ( thisRES > HI_RES )
#endif
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
#if (0)
                    /* NVDI 640x480 Monochrome 68 Hz, 34.0 KHz */
                    else if ( xcb->MISC_Wr == 0xE7 )
                    {
                        windowWidth = 640;
                        windowHeight = 480;
                        COLOURDEPTH = 1;
                    }

                    /* NVDI 800x600 Monochrome 60 Hz, 38.0 KHz */
                    else if ( xcb->MISC_Wr == 0xE3 )
                    {
                        windowWidth = 800;
                        windowHeight = 600;
                        COLOURDEPTH = 1;
                    }

                    /* NVDI 1024x768 Monochrome 60 Hz, 50.0 KHz */
                    else if ( xcb->MISC_Wr == 0x2B )
                    {
                        windowWidth = 1024;
                        windowHeight = 768;
                        COLOURDEPTH = 1;
                    }   

                    /* NVDI 1280x960 Monochrome 50 Hz, 54.0 KHz */
                    else if ( xcb->MISC_Wr == 0xA7 )
                    {
                        windowWidth = 1280;
                        windowHeight = 960;
                        COLOURDEPTH = 1;
                    }  
#endif 
                }

                //else if ( et4000Res == 447 )
                //{
                //    windowWidth = 640;
                //    windowHeight = 400;
                //    COLOURDEPTH = 1;
                //}  

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
                    vinfo.xres_virtual = windowWidth;
                    vinfo.yres_virtual = windowHeight;
                    vinfo.bits_per_pixel = 16;

                    if ( COLOURDEPTH == 5 )
                        vinfo.bits_per_pixel = 32;

                    if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
                    {
                        printf ( "ioctl error setting virtual screen\n" );
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
                            memset ( fbp, 0xFF, screensize );

                            RTGresChanged = 0; 
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
        }

        RTG_VSYNC = 0;


        /* draw the screen */
#ifdef NATIVE_RES
        if ( thisRES < RESOLUTION1 )
            draw ( thisRES );

        else
#endif
        /* only draw screen if VGA sub-system has been enabled */
        if ( !RTGresChanged && !unknown )
        {
            //while ( PS_LOCK );
            //RTG_LOCK = true;
            //while ( RTG_LOCK );
            //if ( !RTG_LOCK )
            //{
                et4000Draw ( windowWidth, windowHeight );
            //}
            //RTG_LOCK = false;
        }

        RTG_VSYNC = 1;
        took = 0;

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

            //usleep ( 100 );
        }

        /* debug */
        //if ( took > (FRAME_RATE + 200) )
            //printf ( "[ET4000] Frame Overrun - Expected %dus, Actual %dus\n", FRAME_RATE, took );
        //    printf ( "[ET4000] Frame Overrun\n" );
    }

    /* if we are here then emulation has ended, hopefully from a user interrupt */
    ET4000enabled = false;

    /* free up RTG memory */
    if ( RTGbuffer )
       // free ( RTGbuffer );
        munmap ( RTGbuffer, MAX_VRAM );

    close ( fbfd );
}


void rtg ( int size, uint32_t address, uint32_t data ) 
{

    if ( size == OP_TYPE_BYTE )
    {
        *(uint8_t *)( screen + (address - RTG_ATARI_SCREEN_RAM) ) = data & 0xff;
    }

    else if ( size == OP_TYPE_WORD )
    {
        *(uint16_t *)( screen + (address - RTG_ATARI_SCREEN_RAM) ) = be16toh ( data & 0xffff );
    }

    else if ( size == OP_TYPE_LONGWORD )
    {
        *(uint32_t *)( screen + (address - RTG_ATARI_SCREEN_RAM) ) = be32toh ( data );
    }
}


void rtgInit ( void )
{
    char func [] = "[RTG] ";


    // Open the file for reading and writing
    fbfd = open ( "/dev/fb0", O_RDWR );

    if ( !fbfd ) 
    {
        printf ( "%sFATAL: cannot open framebuffer device.\n", func );
        return;
    }

    printf ( "%sFramebuffer device open\n", func );

    fbp = NULL;    

    /* Atari screen buffer - not used for ET4000 */
    /*
    if ( ( screen = (volatile void *)calloc ( 640 * 400, 1 ) ) == NULL )
    {
        printf ( "%sFATAL - failed to allocate memory to screen buffer\n", func );
        return;
    }
    */
    //RTG_ATARI_SCREEN_RAM = ATARI_VRAM_BASE;
    //RTG_RES = RESOLUTION1;
    //RTGresChanged = 1;

   if ( RTG_fps )
   {
        if ( RTG_fps > 75 )
            RTG_fps = 75;
            
        FRAME_RATE = (float)( 1.0 / RTG_fps ) * 1000000.0;
   }

    else 
    {
        RTG_fps = 60;
        FRAME_RATE = HZ60;
    }

    printf ( "%sFPS %ld\n", func, RTG_fps );
}


/* ET4000 */


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

    ET4000enabled = false;
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

    //memcpy ( (void *)xcb->user_palette, palette16, sizeof palette16 );
    for ( int i = 0, a = 0; i < 16; i++, a++ )
    {
        
       // xcb->user_palette [a++] = palette16 [i] & 0x1f; //(palette16 [i] >> 11) & 0x1f;
       // xcb->user_palette [a++] = (palette16 [i] >> 5) & 0x2f;
      //  xcb->user_palette [a]   = (palette16 [i] >> 11) & 0x1f; //palette16 [i] & 0x1f;
    }

    

    /* this might be a re-initialise, so don't allocate more memory */
    if ( RTGbuffer == NULL )
    {
        VRAMbuffer = malloc ( MAX_VRAM );
        //RTGbuffer = malloc ( MAX_VRAM ); /* allocate max size */
        RTGbuffer = mmap ( NULL, MAX_VRAM, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );

        RTG_VSYNC = 1;

        //if ( RTGbuffer == NULL )
        if ( RTGbuffer == MAP_FAILED || VRAMbuffer == (void *)NULL )
        {
            printf ( "[RTG] ET4000 Initialisation failed - %d\n", errno );

            ET4000Initialised = false;

            return 0;
        }

        ET4000Initialised = true;

        logo ();

        first = true;
    }

    RTG_LOCK = false;

    return 1;
}


//extern volatile bool RTG_RAMLOCK;
extern volatile bool RAMLOCK;

void et4000Draw ( int windowWidth, int windowHeight )
{
    static void     *dst;
    static void     *src;    
    static uint8_t  plane0, plane1, plane2, plane3;
    static uint32_t address;
    static uint32_t pixel;
    static int      SCREEN_SIZE;
    static struct   timeval stop, start;
        
    src = (void *)NULL; 
    dst = (void *)NULL;
    SCREEN_SIZE = windowWidth * windowHeight;


    while ( RTG_LOCK );

    //PS_LOCK = true;

    memcpy ( RTGbuffer, VRAMbuffer, MAX_VRAM );

    /* Monochrome */
    if ( COLOURDEPTH == 1 )
    {
        uint16_t *dptr = fbptr;
        uint8_t  *sptr = RTGbuffer;

        for ( uint32_t address = 0, pixel = 0; pixel < SCREEN_SIZE; address++ ) 
        {
            for ( int ppb = 0; ppb < 8; ppb++ )
            {
                dptr [pixel++] = ( sptr [address] >> (7 - ppb) ) & 0x1 ? 0x0020 : 0xffff;
            }
        }
    }

    /* Colour 8bit 256 colours */
    else if ( COLOURDEPTH == 2 )
    {
        uint16_t *dptr = fbptr;
        uint8_t  *sptr = RTGbuffer;
        int      ix;
        uint8_t  r, g, b;
        int      x;
        int      y;

        for ( address = 0, pixel = 0; pixel < SCREEN_SIZE; pixel++, address++ ) 
        {
            ix = sptr [address] * 3;                // pointer to palette index
            
            r  = xcb->user_palette [ix++] >> 1;     // 6 bit red to 5 bit
            g  = xcb->user_palette [ix++];          // 6 bit green 
            b  = xcb->user_palette [ix] >> 1;       // 6 bit blue t 5 bit

            dptr [ pixel ] = r << 11 | g << 5 | b;
        }
    }

    /* Colour 4bit 16 colours */
    /* TODO - cryptodad - IS THIS REALLY NEEDED??? WHO WANTS 16 colours when >= 256 is available */
    else if ( COLOURDEPTH == 3 )
    {
        uint16_t *dptr = fbptr;
        uint8_t *sptr = RTGbuffer;
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
        uint16_t *dptr = fbptr;
        uint16_t *sptr = RTGbuffer;
        

        for ( pixel = 0; pixel < SCREEN_SIZE; pixel++ )
        {
            dptr [pixel] = sptr [pixel];
        }
    }

    /* Colour 24bit 16M colours */
    else if ( COLOURDEPTH == 5 )
    {
        uint32_t *dptr = (uint32_t *)fbptr;
        uint8_t *sptr = RTGbuffer;
        

        for ( address = 0, pixel = 0; pixel < SCREEN_SIZE; ) 
        {
            dptr [pixel++] = (uint32_t)( sptr [address++] << 16 | sptr [address++] << 8 | sptr [address++] );
        }
    }

    //PS_LOCK = false;
}


uint32_t et4000Read ( uint32_t addr, uint32_t *value, int type )
{
    static uint32_t a;
    static uint32_t offset;


    if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGTOP )
    {
        *value = 1;

       // printf ( "ET4000 reg read 0x%X\n", addr );

        if ( (addr - NOVA_ET4000_REGBASE) < 0x50 )
            a = 0x3B0 + (addr - NOVA_ET4000_REGBASE);
        
        else
            a = addr & 0x3FF;

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
                printf ( "ET4000 iw register = 0x%X\n", ix );
                break;
            case 0x3cc: /* GENERAL register - Miscellaneous Output */
                *value = xcb->MISC_Wr;
                break;
            case 0x3ca: /* Feature Control Register */
                *value = xcb->FCr;
                break;
            case 0x3c7:
                break;
            case 0x3c1:
                break;
            case 0x3d4:
                break;
            case 0x3d5:
                break;
            case 0x3c4:
                *value = xcb->ts_ix;
              //  printf ( "read reg 0x%X TS INDEX = 0x%x\n", a, *value );
                break;
            case 0x3c5:
                *value = xcb->ts_index [xcb->ts_ix];
               // printf ( "read reg 0x%X TS REGISTER %d = 0x%x\n", a, xcb->ts_ix, *value );
                break;
            case 0x3c3: /* Video Subsystem Register */
            case 0x46e8:                

                /* stop emutos initialising ET4000 */
                if ( first )
                {
                    first = false;
                    *value = 0xff;

                    if ( !RTG_EMUTOS_VGA )
                        g_buserr = 1; /* raise a BERR if not using EMUtos to init ET4000 */
                }

                else
                    *value = xcb->videoSubsystemr;

                break;
            case 0x3c2: /* Input Status Register Zero */
                *value = xcb->ISr0;
                break;
            case 0x3c6: /* PEL Mask */
                *value = 0x0f; /* TODO */
                break;
            case 0x3c8: /* */
                break;
            case 0x3c9:
                *value = xcb->user_palette [xcb->palette_ix_rd];
                break;
            case 0x3cd:
                break;
            case 0x3ce:
                *value = xcb->gdc_ix;
                break;
            case 0x3cf:
                *value = xcb->gdc_index [xcb->gdc_ix];
                break;
            case 0x3da: /* GENERAL register - Input Status Register One */

                *value = (~RTG_VSYNC << 7) | (RTG_VSYNC << 3) | ET4000enabled;
                xcb->atc_ixdff = false; /* reset index/data flip-flop */
                break;

            default:
                printf ( "ET4000 unknown read register 0x%X\n", a );

                //g_buserr = 1;

                return 1;
        }

        return 1;
    }

    else
    {
        offset = addr - NOVA_ET4000_VRAMBASE;
        //printf ( "et4000Read () -> type = %d, addr = 0x%X\n", type, addr );
        RTG_LOCK = true;

        if ( type == OP_TYPE_BYTE )
            *value = *( uint8_t *)( VRAMbuffer + offset );

        else if ( type == OP_TYPE_WORD )
            *value = be16toh ( *( uint16_t *)( VRAMbuffer + offset ) );

        else if ( type == OP_TYPE_LONGWORD )
            *value = be32toh ( *(uint32_t *)( VRAMbuffer + offset ) );

        RTG_LOCK = false;

        return 1;
    }

    return 0;
}


uint32_t et4000Write ( uint32_t addr, uint32_t value, int type )
{
    static uint32_t offset;
    static uint32_t a;
    static int invalid = 0;


    if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGTOP )
    {
        //printf ( "ET4000 reg write 0x%X\n", addr );

        if ( (addr - NOVA_ET4000_REGBASE) < 0x50 )
            a = 0x3B0 + (addr - NOVA_ET4000_REGBASE);
        
        else
            a = addr & 0x3FF;

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

            case 0x3cd: /* GDC Segment Select */

                if ( xcb->KEY && !(xcb->crtc_index [VSCONF1] & 0x10) )
                {
                    xcb->gdc_segment_select = value & 0xFF;
                    printf ( "GDC Segment Select 0x%X\n", xcb->gdc_segment_select );
                }

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
            case 0x3c4: /* TS Index */
                xcb->ts_ix = value & 0x07;
                break;
            case 0x3c5: /* TS Indexed Register 2: Write Plane Mask */

                if ( xcb->ts_ix == 2 )
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
            case 0x3c2: /* GENERAL register - Miscellaneous Output Register */
                printf ( "MISC_W using CRTC addresses %s\n",
                    value & 0x01 ? "3Dx Colour" : "3Bx Monochrome" );
                printf ( "MISC_W DMA access %s\n",
                    value & 0x02 ? "enabled" : "disabled" );
                
                /* ref - Tseng Labs pg32 2.2.2 */
                /* 
                 * cs [1,0] bits (clock select)
                 * 0 = VGA Mode
                 * 1 = VGA mode/CGA
                 * 2 = EGA mode
                 * 3 = Extended mode 
                 * */
                //printf ( "MISC_W MCLK = 0x%X\n", value & 0x0c );
               // printf ( "MISC_W CS bits = 0x%X\n", value & 0x03 );
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
            case 0x3c0: /* ATC (Attribute Controller) register - Address/Index (23 registers) */
                
                if ( xcb->atc_ixdff == false ) /* index */
                    xcb->atc_ix = value & 0x1f;

                if ( xcb->atc_ixdff == true )   /* data */
                    xcb->atc_index [xcb->atc_ix] = value;

                /* every write toggles the index/data bit */
                xcb->atc_ixdff = !xcb->atc_ixdff;
                xcb->atc_paletteRAMaccess = value & 0x20;

                break;
            case 0x3d4: /* 6845 CRT Control Register */
                xcb->crtc_ix = value;
                
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
                printf ( "ET4000 unknown write register 0x%X\n", addr );

                /* occasionaly will see continuous writes to succesive words. If this happens, re-initialise */
                //if ( invalid++ > 5 )
                //{
                    //printf ( "ET4000 too many unknown register writes - reinitialising\n" );

                //    invalid = 0;

                    //et4000Init ();
                //}

                return 0;
        }

        return 1;
    }

    /* should then be a VRAM address */
    
    else
    {
        //printf ( "et4000Write () -> type = %d, addr = 0x%X\n", type, addr );

        offset = addr - NOVA_ET4000_VRAMBASE;

        RTG_LOCK = true;
        /* graphics mode enabled */
        //if ( xcb->gdc_index [6] & 0x01 )
        {
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
        }

        RTG_LOCK = false;

        return 1;
    }

    return 0;
}


/* ------------------------------------------------------------------------- */

/* terminal IO */ 
int kbhit ( void )
{
  int ch;
 
  ch = getchar ();
 
  if ( ch != EOF )
  {
    ungetc ( ch, stdin) ;

    return 1;
  }
 
  return 0;
}


void screenDump ( int w, int h )
{
    FILE fp;
    char *dumpfile = "screendump.raw";
    char filename [13];
    char command [300];

    printf ( "Performing Screendump\n" );
    sprintf ( command, "ffmpeg -vcodec rawvideo -f rawvideo -pix_fmt rgb565le -s %dx%d -i %s -f image2 -frames 1 -hide_banner -y -loglevel quiet -vcodec png screendump.png", w, h, dumpfile );
    system ( "cat /dev/fb0 > screendump.raw" );
    system ( command );
    system ( "bash ./screendump.sh" );

    return;
}


void logo ( void )
{
    uint16_t *dstptr; 
    uint8_t buff;
    uint32_t lineLength;
    uint32_t x, y;
    int start;
    FILE *fp;
    char hdr [0x0f];

    /* bitmap needs to be 8bit no alpha flipped - black & white for now too */
    char *logofile = "configs/AtariLogo800x600.bmp";


    if ( ( fp = fopen ( logofile, "r" ) ) == NULL )
        return;

    /* reallocate framebuffer */
    if ( fbp )
        munmap ( (void *)fbp, screensize );

    if ( ioctl ( fbfd, FBIOGET_VSCREENINFO, &vinfo ) )
    {
        printf ( "logo error getting virtual; width / height\n" );
    }

    vinfo.xres           = 800;
    vinfo.yres           = 600;
    vinfo.xres_virtual   = 800;
    vinfo.yres_virtual   = 600;
    vinfo.bits_per_pixel = 16;

    if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
    {
        printf ( "logo error setting virtual; width / height\n" );
    }

    /* Get fixed screen information */
    if ( ioctl ( fbfd, FBIOGET_FSCREENINFO, &finfo ) ) 
    {
        printf ( "%s Error reading fixed information.\n", __func__ );
    }

    screensize = finfo.smem_len; 
    lineLength = finfo.line_length;

    fbp        = (void *)mmap ( 0, 
                screensize, 
                PROT_READ | PROT_WRITE, 
                MAP_SHARED, 
                fbfd, 
                0 );    

    dstptr     = fbp;
  
    fread ( &hdr, 0x0e, 1, fp ); 
    start = hdr [0x0b] << 8 | hdr [0x0a]; /* read bitmap offset */
    fseek ( fp, start, SEEK_SET );

    /* bitmap origin is bottom right - needs to be top left */
    for ( int pixel = screensize / 2; pixel > 0; pixel-- )
    {  
        fread ( &buff, 1, 1, fp ); 

        dstptr [pixel] =  (buff > 0x80 ? 0x00 : 0xffff) ;
    }
  
    fclose ( fp );
}