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
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/time.h>
#include "../../config_file/config_file.h"

// 'global' variables to store screen info
static int fbfd;
uint16_t *fbp = 0;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
size_t screensize = 0;
uint16_t *fbptr;
volatile uint8_t *screen;

int RTG_enabled = 0;
volatile int RTGresChanged;
volatile uint16_t RTG_PAL_MODE; /* 0 = NTSC, 1 = PAL */
volatile uint32_t RTG_ATARI_SCREEN_RAM;
volatile uint16_t RTG_PALETTE_REG [16]; /* palette colours */
volatile uint8_t  RTG_RES; /* bits 0 & 1 - 00 = 320x200, 01 = 640x200, 10 = 640x400 */

extern volatile int cpu_emulation_running;

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
    WHITE      = 0xffff,
    RED        = 0xf800,
    GREEN      = 0x07e0,
    YELLOW     = 0xffe0,
    BLUE       = 0x001f,
    MAGENTA    = 0xf81f,
    CYAN       = 0x07ff,
    LT_GREY    = 0x7bcf,
    GREY       = 0x5acb,
    LT_RED     = 0xfacb,
    LT_GREEN   = 0x5feb,
    LT_YELLOW  = 0xffef,
    LT_BLUE    = 0x5fff,
    LT_MAGENTA = 0xf897,
    LT_CYAN    = 0xdfff,
    BLACK      = 0

} palette;

#define ATARI_VRAM_BASE   0x003f8000 /* 4MB machine - will get changed at detection */
#define VRAM ((640 * 400) / 8)    /* 32 KB 640 * 400 = 8 pixels per word */

#define LOW_RES 0
#define MED_RES 1
#define HI_RES  2

#define RGBalpha 0x0000 

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
    int X_SCALE, Y_SCALE;
    int ABPP;
    volatile uint16_t *sptr =(volatile uint16_t *)screen;

    
    if ( res == HI_RES )
    {
        ABPP = 1;
        X_SCALE = 1;
        Y_SCALE = 1;
    }

    else if ( res == MED_RES )
    {
        ABPP = 2;
        X_SCALE = 1;
        Y_SCALE = 1;
    }

    else if ( res == LOW_RES )
    {
        ABPP = 4;
        X_SCALE = 2;
        Y_SCALE = 1;
    }

    /* VRAM = 32 KB so divide by 2 for 16 bit WORDS */
    for ( uint32_t address = 0, pixel = 0; address < VRAM / 2; address += ABPP ) 
    {
        if ( res == HI_RES )
        {
            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                
                x = pixel % 640;
                y = pixel / 640;

                zoomY = y * Y_SCALE;
                zoomX = x * X_SCALE;

                *( fbptr + zoomX + (zoomY * finfo.line_length) ) = plane0 == 1 ? BLACK : WHITE; 

                if ( X_SCALE > 1 )
                    memcpy ( fbptr + zoomX + zoomY, fbptr + zoomX + zoomY, X_SCALE );

                if ( Y_SCALE > 1 )
                    memcpy ( fbptr + zoomX + zoomY, fbptr + zoomX + zoomY, 640 * Y_SCALE );

                pixel++;
            }    
        }

        if ( res == MED_RES )
        {
            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                plane1 = ( be16toh ( sptr [ address + 1 ] ) >> ( 15 - n ) ) & 1; 
                
                x = pixel % 640;
                y = pixel / 640;

                zoomY = y * Y_SCALE;
                zoomX = x * X_SCALE;
            
                *( fbptr + zoomX + (zoomY * finfo.line_length) ) = 
                            RTG_PALETTE_REG [plane1 << 1 | plane0]; 

                if ( X_SCALE > 1 )
                    memcpy ( fbptr + zoomX + zoomY, fbptr + zoomX + zoomY, X_SCALE );

                if ( Y_SCALE > 1 )
                    memcpy ( fbptr + zoomX + zoomY, fbptr + zoomX + zoomY, 640 * Y_SCALE );

                pixel++; 
            }
        }

        else if ( res == LOW_RES )
        {
            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                plane1 = ( be16toh ( sptr [ address + 1 ] ) >> ( 15 - n ) ) & 1; 
                plane2 = ( be16toh ( sptr [ address + 2 ] ) >> ( 15 - n ) ) & 1; 
                plane3 = ( be16toh ( sptr [ address + 3 ] ) >> ( 15 - n ) ) & 1; 

                x = pixel % 320;
                y = pixel / 320;

                zoomY = y * Y_SCALE;
                zoomX = x * X_SCALE;
              
                *( fbptr + zoomX + (zoomY * finfo.line_length) ) = 
                            RTG_PALETTE_REG [ plane3 << 3 | plane2 << 2 | plane1 << 1 | plane0]; 

                if ( X_SCALE > 1 )
                    memcpy ( fbptr + zoomX + zoomY, fbptr + zoomX + zoomY, X_SCALE );

                if ( Y_SCALE > 1 )
                    memcpy ( fbptr + zoomX + zoomY, fbptr + zoomX + zoomY, 320 * Y_SCALE );

                pixel++;
            }  
        }  
    }
}


void *rtgRender ( void* vptr ) 
{
    int wait;
    int render = 1;
    int thisRES = LOW_RES;
    int FRAME_RATE = 20000;

    while ( !cpu_emulation_running )
        ;

    while ( cpu_emulation_running )
    {
        if ( RTGresChanged )
        {
            thisRES = RTG_RES;

            if ( thisRES == LOW_RES )
            {
#if (0)
                //vinfo.xres = 1280;
                //vinfo.yres = 800;
                vinfo.xres_virtual = 320;
                vinfo.yres_virtual = 200;
                vinfo.bits_per_pixel = 16;
                if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ))
                {
                    printf ( "Error setting virtual; width / height\n" );
                }

                else
                {
                    printf ( "rtgRender: resolution change to LOW_RES\n" );

                    memcpy ( (void *)RTG_PALETTE_REG, palette16, sizeof palette16 );
                }
#else
                printf ( "rtgRender: resolution change to LOW_RES\n" );

                memcpy ( (void *)RTG_PALETTE_REG, palette16, sizeof palette16 );
#endif
                if ( RTG_PAL_MODE )
                    FRAME_RATE = 20000; 
                
                else
                    FRAME_RATE = 16666; 
            }

            else if ( thisRES == MED_RES )
            {
#if (0)
                //vinfo.xres = 1280;
                //vinfo.yres = 800;
                vinfo.xres_virtual = 640;
                vinfo.yres_virtual = 400;
                vinfo.bits_per_pixel = 16;
                if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ))
                {
                    printf ( "Error setting virtual; width / height\n" );
                }

                else
                {
                    printf ( "rtgRender: resolution change to MED_RES\n" );

                    memcpy ( (void *)RTG_PALETTE_REG, palette4, sizeof palette4 );
                }
#else
                printf ( "rtgRender: resolution change to MED_RES\n" );

                memcpy ( (void *)RTG_PALETTE_REG, palette4, sizeof palette4 );
#endif
                if ( RTG_PAL_MODE )
                    FRAME_RATE = 20000; 
                
                else
                    FRAME_RATE = 16666; 
            }

            else if ( thisRES == HI_RES )
            {
#if (1)
                //vinfo.xres = 1280;
                //vinfo.yres = 800;
                vinfo.xres_virtual = 640;
                vinfo.yres_virtual = 400;
                vinfo.bits_per_pixel = 16;
                if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
                {
                    printf ( "Error setting virtual; width / height\n" );
                }

                else
                    printf ( "rtgRender: resolution change to HI_RES\n" );
#else
                printf ( "rtgRender: resolution change to HI_RES\n" );
#endif
                FRAME_RATE = 14285;
            }

            for ( int i = 0; i < screensize / (vinfo.bits_per_pixel / 8); i++ ) 
            {
                *(uint16_t *)( ( fbptr + i ) ) = RGBalpha | BLACK;
            }

            RTGresChanged = 0;
        }

        draw ( thisRES );

        /* 50 Hz 20000 (20ms), 60 Hz 16666 (16.6ms), 70 Hz 14285 (14.2ms) */
        wait = FRAME_RATE;

        /* delay for required time or until resolution change has been signalled */
        while ( wait > 0 && RTGresChanged == 0 )
        {
            usleep ( 100 );

            wait = wait - 100;
        }
    }
}


void *rtg ( int size, uint32_t address, uint32_t data ) 
{
   // while ( RTGresChanged )
    //;

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

    /* check .cfg has setvar rtg */
    if ( !RTG_enabled )
        return;

    // Open the file for reading and writing
    fbfd = open ( "/dev/fb0", O_RDWR );

    if ( !fbfd ) 
    {
        printf ( "%sError: cannot open framebuffer device.\n", func );
        return;
    }

    printf ( "%sFramebuffer device open\n", func );

    // Get variable screen information
    if ( ioctl ( fbfd, FBIOGET_VSCREENINFO, &vinfo ) ) 
    {
        printf("%sError reading variable information.\n", func );
        return;
    }

    printf ( "%sHDMI resolution %dx%d, %dbpp\n", func, vinfo.xres, vinfo.yres, vinfo.bits_per_pixel );

    /* /boot/config.txt - do not configure hdmi - leave to auto detect */
    /* need xres and yres doubled to handle HI_RES for some reason */
    //vinfo.xres = 1280;
    //vinfo.yres = 800;
    vinfo.xres_virtual = 320;
    vinfo.yres_virtual = 200;
    vinfo.bits_per_pixel = 16;

    if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
    {
        printf ( "%sError setting virtual; width / height\n", func );
    }

    // Get fixed screen information
    if ( ioctl ( fbfd, FBIOGET_FSCREENINFO, &finfo ) ) 
    {
        printf ( "%sError reading fixed information.\n"), func ;
    }

    // map fb to user mem 
    screensize = finfo.smem_len; 
    
    fbp = (uint16_t *)mmap ( 0, 
                        screensize, 
                        PROT_READ | PROT_WRITE, 
                        MAP_SHARED, 
                        fbfd, 
                        0);

    if ( (int)fbp == -1 ) 
    {
        printf ( "%sFATAL - failed to mmap.\n", func );
        return;
    }

    
    int i;
    fbptr = (uint16_t *)fbp;

    //printf ( "screen RAM size is 0x%X\n", screensize );
    //printf ( "screen line length is %d\n", finfo.line_length );

    if ( ( screen = (volatile uint8_t *)calloc ( (640 * 400) / 8, 1 ) ) == NULL )
    {
        printf ( "%sFATAL - failed to allocate memory to screen\n", func );
        return;
    }

    RTG_ATARI_SCREEN_RAM = ATARI_VRAM_BASE;
    RTG_RES = LOW_RES;
    RTGresChanged = 1;
}


