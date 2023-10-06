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
#include <sys/time.h>
#include "../../config_file/config_file.h"
#include <stdbool.h>
#include "et4000.h"


#define MAX_WIDTH  1024
#define MAX_HEIGHT 768
#define MAX_VRAM   (1024 * 768 * 2)
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
volatile int COLOURDEPTH = 0;
volatile uint32_t RTG_VSYNC;
volatile uint8_t *screen;
volatile int RTGresChanged = 0;
volatile uint16_t RTG_PAL_MODE; /* 0 = NTSC, 1 = PAL */
volatile uint32_t RTG_ATARI_SCREEN_RAM;
volatile uint16_t RTG_PALETTE_REG [16]; /* palette colours */
int ix;
bool RTG_enabled = false;
bool screenGrab = false;

extern volatile uint32_t RTG_VRAM_BASE;
extern volatile uint32_t RTG_VRAM_SIZE;
extern struct emulator_config *cfg;
extern volatile int cpu_emulation_running;
extern volatile int g_buserr;
extern volatile int cpu_emulation_running;

// 'global' variables to store screen info
static int fbfd;
void *fbp = 0;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
size_t screensize = 0;
void *fbptr;

void et4000Draw ( int, int );
int FRAME_RATE = HZ60;

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
    int    wait;
    int    took;
    int    windowWidth;
    int    windowHeight;
    int    SCREEN_SIZE;
    struct timeval stop, start;


    while ( !cpu_emulation_running )
        ;

    while ( cpu_emulation_running )
    {
        /* get time and wait for end-of-frame */
        /* 50 Hz 20000 (20ms), 60 Hz 16666 (16.6ms), 70 Hz 14285 (14.2ms) */
        gettimeofday ( &start, NULL );

        if ( ET4000enabled && RTGresChanged )
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
            if ( (xcb->VGAmode & 0x80) == 0x80 )
            {
                if ( GETRES () == 523 )
                {
                    windowWidth = 640;
                    windowHeight = 480;
                }

                else if ( GETRES () == 626 )
                {
                    windowWidth = 800;
                    windowHeight = 600;
                }

                else if ( GETRES () == 803 )
                {
                    windowWidth = 1024;
                    windowHeight = 768;
                }

                else 
                {
                    windowWidth = 640;
                    windowHeight = 480;
                }

                vinfo.xres = windowWidth;
                vinfo.yres = windowHeight;
                vinfo.xres_virtual = windowWidth;
                vinfo.yres_virtual = windowHeight;
                vinfo.bits_per_pixel = 16;

                if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
                {
                    printf ( "RESOLUTION2 error setting virtual; width / height\n" );
                }

                else
                    printf ( "[RTG] Resolution change to %dx%d ", windowWidth, windowHeight );

                /*
                * **************************
                * check for Linear or Planar
                * ************************** 
                */

                /* Chain 4 (Linear) */
                if ( (xcb->ts_index [4] & 0x08) == 0x08 && xcb->ts_index [2] == 0x0F )
                {
                    /* res 256 colours */
                    if ( xcb->ts_index [7] == 0xF4 && (xcb->MISC_Wr & 0x0C) == 0x00 || (xcb->MISC_Wr & 0x0C) == 0x08 ) 
                    {
                        COLOURDEPTH      = 2; 
                        SCREEN_SIZE      = windowWidth * windowHeight;
                    }
                    
                    /* res 32K/64K colours */
                    // TS Memory mode = 0xE
                    // TS Auxillary mode = 0xB4
                    // MISC_W MCLK = 0x0
                    else if ( xcb->ts_index [7] == 0xB4 && (xcb->MISC_Wr & 0x0C) == 0x00 )
                    {
                        COLOURDEPTH      = 4;
                        SCREEN_SIZE      = windowWidth * windowHeight;
                    }
                
                    /* Chain 4 (Linear) 24bit */
                    // TS Memory mode = 0xE
                    // TS Auxillary mode = 0xB4
                    // MISC_W MCLK = 0xC
                    if ( xcb->ts_index [7] == 0xB4 && (xcb->MISC_Wr & 0x0C) == 0x0C )
                    {
                        COLOURDEPTH      = 5;
                        SCREEN_SIZE      = windowWidth * windowHeight;
                    }
                }

                /* Planar */
                else if ( (xcb->ts_index [4] & 0x08) == 0x00 )
                {
                    /* Monochrome */
                    if ( xcb->ts_index [2] == 0x01 ) // plane mask
                    {                    
                        COLOURDEPTH      = 1;
                        SCREEN_SIZE      = windowWidth * windowHeight;
                    }

                    /* Colour */
                    else if ( xcb->ts_index [2] == 0x0F ) // plane mask
                    {                    
                        COLOURDEPTH      = 3;
                        SCREEN_SIZE      = windowWidth * windowHeight * 2;
                    }

                    else 
                    {
                        SCREEN_SIZE      = windowWidth * windowHeight;
                    }
                }
                
                printf ( "%d bpp\n", COLOURDEPTH == 1 ? 1 : COLOURDEPTH == 2 ? 8 : COLOURDEPTH == 3 ? 4 : COLOURDEPTH == 4 ? 16 : 24 );
            }

            /* reallocate framebuffer */
            if ( fbp )
                munmap ( (void *)fbp, screensize );

            if ( COLOURDEPTH == 5 )
            {
                vinfo.xres = windowWidth;
                vinfo.yres = windowHeight;
                vinfo.xres_virtual = windowWidth;
                vinfo.yres_virtual = windowHeight;
                vinfo.bits_per_pixel = 32;

                if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
                {
                    printf ( "COLOURDEPTH 5 error setting virtual; width / height\n" );
                }
            }

            /* Get fixed screen information */
            if ( ioctl ( fbfd, FBIOGET_FSCREENINFO, &finfo ) ) 
            {
                printf ( "%s Error reading fixed information.\n", __func__ );
            }

            screensize = finfo.smem_len; 

            fbp = (void *)mmap ( 0, 
                        screensize, 
                        PROT_READ | PROT_WRITE, 
                        MAP_SHARED, 
                        fbfd, 
                        0 );

            if ( COLOURDEPTH == 5 )
                fbptr = (uint32_t *)fbp;  

            else 
                fbptr = (uint16_t *)fbp;    

            /* clear screen to WHITE */
            //memset ( fbptr, 0xFF, screensize );
            memset ( RTGbuffer, 0x00, MAX_VRAM );

            RTGresChanged = 0;            
        }

        RTG_VSYNC = 0;

        /* draw the screen */
#ifdef NATIVE_RES
        if ( thisRES < RESOLUTION1 )
            draw ( thisRES );

        else
#endif
        /* only draw screen if VGA sub-system has been enabled */
        //if ( ET4000enabled )
        if ( ET4000enabled && !RTGresChanged )
            et4000Draw ( windowWidth, windowHeight );

        RTG_VSYNC = 1;
        took = 0;

        /* rough frame rate delay */
        while ( 1 )
        {
            /* simple screengrab - could be improved A LOT */
            /* pressing 'p' on the keyboard executes a system call to grab the framebuffer */
            /* followed by a second system call to ffmpeg which converts the screendump to a .png file */
            
            //if ( screenGrab && thisRES > HI_RES && kbhit () )
            if ( screenGrab && kbhit () )
            {
                int c = getchar ();

                if ( c == 's' || c == 'S' )
                    screenDump ( windowWidth, windowHeight );
            }
            
            gettimeofday ( &stop, NULL );

            took = ( (stop.tv_sec - start.tv_sec) * 1000000 ) + (stop.tv_usec - start.tv_usec);
            
            if ( took >= FRAME_RATE )
                break;

            //usleep ( 100 );
        }

        /* debug */
        if ( took > FRAME_RATE + 200 )
            printf ( "[ET4000] Frame Overrun - Expected %dus, Actual %dus\n", FRAME_RATE, took );
    }

    /* if we are here then emulation has ended probably from a user break */
    ET4000enabled = false;
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

    /* check .cfg has setvar rtg */
    //if ( !RTG_enabled )
    //    return;

    // Open the file for reading and writing
    fbfd = open ( "/dev/fb0", O_RDWR );

    if ( !fbfd ) 
    {
        printf ( "%sFATAL: cannot open framebuffer device.\n", func );
        return;
    }

    printf ( "%sFramebuffer device open\n", func );

    /* /boot/config.txt - do not configure hdmi - leave to auto detect */
    /* need xres and yres doubled to handle HI_RES for some reason */
    /*
    vinfo.xres = 1920;
    vinfo.yres = 1080;
    vinfo.xres_virtual = 320;
    vinfo.yres_virtual = 200;
    vinfo.bits_per_pixel = 16;

    if ( ioctl ( fbfd, FBIOPUT_VSCREENINFO, &vinfo ) )
    {
        printf ( "%sError setting virtual; width / height\n", func );
    }
    */
    // Get fixed screen information
    //if ( ioctl ( fbfd, FBIOGET_FSCREENINFO, &finfo ) ) 
    //{
    //    printf ( "%sError reading fixed information.\n"), func ;
    //}

    // map fb to user mem 
    //screensize = finfo.smem_len; 
    //printf ( "%sScreen size is 0x%X\n", func, screensize );
    //printf ( "%sScreen line length is %d\n", func, finfo.line_length );

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
   // RTGresChanged = 1;
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
    xcb->videoSubsystemr = 0x01; /* configure port address 0x3c3 */
    //xcb->videoSubsystemr = 0x08; /* configure port address 0x46e8 */
    xcb->atc_ixdff = false;
    xcb->atc_ix = 0;
    xcb->KEY = false;
    xcb->ISr0 = 0;
    xcb->FCr = 0;

    first = true;

    /* this might be a re-initialise, so don't allocate more memory */
    if ( RTGbuffer == NULL )
        RTGbuffer = malloc ( MAX_VRAM ); /* allocate max size */

    RTG_VSYNC = 1;

    if ( RTGbuffer == NULL )
    {
        printf ( "[RTG] ET4000 Initialisation failed\n" );
        ET4000Initialised = false;

        return 0;
    }

    ET4000Initialised = true;

    logo ();

    return 1;
}


extern volatile bool RTG_RAMLOCK;

void et4000Draw ( int windowWidth, int windowHeight )
{
    void     *dst = NULL;
    void     *src = NULL;    
    uint8_t  plane0, plane1, plane2, plane3;
    int      ABPP;
    uint32_t address;
    uint32_t pixel;
    int      SCREEN_SIZE = windowWidth * windowHeight;
    struct   timeval stop, start;
        

    /* Monochrome */
    if ( COLOURDEPTH == 1 )
    {
        uint16_t *dptr = fbptr;
        uint8_t  *sptr = RTGbuffer;

        for ( uint32_t address = 0, pixel = 0; pixel < SCREEN_SIZE; address++ ) 
        {
            for ( int ppb = 0; ppb < 8; ppb++ )
            {
                while ( RTG_RAMLOCK );
            
                //*( dptr + pixel ) = ( *( sptr + address ) >> ( 7 - ppb ) ) & 0x1 ? 0x20 : 0xffff;
                dptr [pixel++] = ( sptr [address] >> (7 - ppb) ) & 0x1 ? 0x0020 : 0xffff;
            }
        }
        
    }

    /* Colour 8bit 256 colours */
    else if ( COLOURDEPTH == 2 )
    {
        uint16_t *dptr = fbptr;
        uint8_t  *sptr = (uint8_t *)RTGbuffer;
        int      ix;
        uint8_t  r, g, b;
        int      x;
        int      y;

        for ( address = 0, pixel = 0; pixel < SCREEN_SIZE; pixel++, address++ ) 
        {
            while ( RTG_RAMLOCK );

            ix = sptr [address] * 3;                // pointer to palette index
            
            r  = xcb->user_palette [ix++] >> 1;     // 6 bit red to 5 bit
            g  = xcb->user_palette [ix++];          // 6 bit green 
            b  = xcb->user_palette [ix] >> 1;       // 6 bit blue t 5 bit

            x = pixel % windowWidth;
            y = pixel / windowWidth;
           
            dptr [ x + (y * windowWidth) ] = r << 11 | g << 5 | b;
        }
    }

    /* Colour 4bit 16 colours */
    /* TODO - cryptodad - IS THIS REALLY NEEDED??? WHO WANTS 16 colours when >= 256 is available */
    else if ( COLOURDEPTH == 3 )
    {
        uint16_t *dptr = fbptr;
        uint8_t *sptr = RTGbuffer;
        //ABPP = 4;
        uint32_t colour;
        uint8_t r, g, b;
        int plane0, plane1, plane2, plane3;
        int x;
        int y;
        int zoomX, zoomY;
        int X_SCALE = 1;
        int Y_SCALE = 1;
        
        for ( uint32_t address = 0, pixel = 0; pixel < SCREEN_SIZE; address += 4 ) 
        {
            for ( int ppb = 0; ppb < 8; ppb++, pixel++ )
            {
#if (0)
                plane0 = xcb->ts_index [2] & 0x01 ? ( be16toh ( sptr [address] ) >> ( 15 - ppb ) ) & 0x1 : 0; // Blue      
                plane1 = xcb->ts_index [2] & 0x02 ? ( be16toh ( sptr [address + 1] ) >> ( 15 - ppb ) ) & 0x1 : 0; // Green     
                plane2 = xcb->ts_index [2] & 0x04 ? ( be16toh ( sptr [address + 2] ) >> ( 15 - ppb ) ) & 0x1 : 0; // Red       
                plane3 = xcb->ts_index [2] & 0x08 ? ( be16toh ( sptr [address + 3] ) >> ( 15 - ppb ) ) & 0x1 : 0; // Intensity 

                colour = vga_palette [plane3 << 3 | plane2 << 2 | plane1 << 1 | plane0];
#else
                plane0 = ( ( sptr [address    ] ) >> ( 7 - ppb ) ) & 0x1; // Blue      
                plane1 = ( ( sptr [address + 1] ) >> ( 7 - ppb ) ) & 0x1; // Green     
                plane2 = ( ( sptr [address + 2] ) >> ( 7 - ppb ) ) & 0x1; // Red       
                plane3 = ( ( sptr [address + 3] ) >> ( 7 - ppb ) ) & 0x1; // Intensity 

                colour = vga_palette [plane3 << 3 | plane2 << 2 | plane1 << 1 | plane0];
#endif
                r = ((colour >> 16) & 0xff) >> 3;
                g = ((colour >> 8) & 0xff) >> 2;
                b = (colour & 0xff) >> 3;
                
                x = pixel % windowWidth;
                y = pixel / windowWidth;
                zoomY  = y * Y_SCALE;
                zoomX  = x * X_SCALE;
                dptr [ zoomX + (zoomY * windowWidth) ] = r << 11 | g << 5 | b;
                //dptr [pixel++] = r << 11 | g << 5 | b;
            }
            //printf ( "SCREEN SIZE %d - pixel %d, address %d\n", SCREEN_SIZE, pixel, address );
        }
        
    }
    
    /* Colour 16bit 32K/64K colours */
    else if ( COLOURDEPTH == 4 )
    {
        uint16_t *dptr = fbptr;
        uint16_t *sptr = RTGbuffer;
        
        for ( pixel = 0; pixel < SCREEN_SIZE; pixel++ )
        {
            while ( RTG_RAMLOCK );

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
            while ( RTG_RAMLOCK );

            dptr [pixel++] = (uint32_t)( sptr [address++] << 16 | sptr [address++] << 8 | sptr [address++] );
        }
    }
}


uint32_t et4000Read ( uint32_t addr, uint32_t *value, int type )
{
    
    if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGTOP )
    {
        *value = 1;

       // printf ( "ET4000 reg read 0x%X\n", addr );

        uint32_t a = addr - NOVA_ET4000_REGBASE;

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
                break;
            case 0x3c5:
                break;
            case 0x3c3: /* Video Subsystem Register */
            case 0x46e8:
                /* if emulator cnf file has not `setenv rtg` then do not enable ET4000 */
                //if ( !ET4000enabled && !RTG_enabled )
                if ( !RTG_enabled )
                {
                //    *value = 0xff;
                //    g_buserr = 1; 
                    printf ( "et4000 raise bus error\n" );
                }

                //else
                    *value = xcb->videoSubsystemr;

                /* stop emutos initialising ET4000 */
                if ( first )
                {
                //    *value = 0;
                    first = false;
                    g_buserr = 1; /* uncomment this line if you don't want EMUtos to init ET4000 */
                }

                break;
            case 0x3c2: /* Input Status Register Zero */
                *value = xcb->ISr0;
                break;
            case 0x3c6: /* */
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
               // printf ( "ET4000 unknown read register 0x%X\n", a );
                break;
        }

        return 1;
    }

//printf ( "et4000Read () -> type = %d, addr = 0x%X\n", type, addr );


    if ( type == OP_TYPE_BYTE )
        *value = *( uint8_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) );

    else if ( type == OP_TYPE_WORD )
        *value = be16toh ( *( uint16_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) ) );

    else if ( type == OP_TYPE_LONGWORD )
        *value = be32toh ( *(uint32_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) ) );

    return 1;
}


uint32_t et4000Write ( uint32_t addr, uint32_t value, int type )
{
    static uint32_t offset;


    if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGTOP )
    {
        uint32_t a = addr - NOVA_ET4000_REGBASE;

        //printf ( "ET4000 reg write 0x%X, 0x%X\n", addr, value );

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
                    xcb->KEY = true;
                break;
            case 0x3ba: /* monochrome */
                break;
            case 0x3cd:
                break;
            case 0x3ce: /* GDC (Graphics Data Controller) Index register */
                xcb->gdc_ix = value & 0x0f;
                break;
            case 0x3cf:
                xcb->gdc_index [xcb->gdc_ix] = value;
                break;
            case 0x3c4: /* TS Index */
                xcb->ts_ix = value & 0x07;
                break;
            case 0x3c5: /* TS Indexed Register 2: Write Plane Mask */

                //if ( xcb->ts_ix == 2 )
                //    printf ( "TS Write Plane Mask = 0x%X\n", value );

                if ( xcb->ts_ix == 4 )
                {
                    if ( (xcb->ts_index [xcb->ts_ix] & 0x08) != (value & 0x08) )
                    {
                       // printf ( "ET4000 Resolution changed\n" );
                        
                        RTGresChanged = 1;
                        //RTG_RES = RESOLUTION3; // arbitrary value - will be set later, but needs to be > HI_RES
                    }
                
                   // printf ( "TS Memory Mode = 0x%X - Map mask = %d, %s mode enabled\n", value, value & 0x04, value & 0x08 ? "Chain 4 (Linear)" : "Planer" );
                }

                else if ( xcb->ts_ix == 7 )
                {
                   // printf ( "TS Auxillary Mode = 0x%X - %s mode enabled\n", value, value & 0x80 ? "VGA" : "EGA" );
                    xcb->VGAmode = value & 0x80;
                }

                xcb->ts_index [xcb->ts_ix] = value;

                break;
            case 0x3c2: /* GENERAL register - Miscellaneous Output */
                //printf ( "MISC_W using CRTC addresses %s\n",
                //    value & 0x01 ? "3Dx Colour" : "3Bx Monochrome" );
                //printf ( "MISC_W DMA access %s\n",
                //    value & 0x02 ? "enabled" : "disabled" );
                //printf ( "MISC_W MCLK = 0x%X\n", value & 0x0c );

                xcb->MISC_Wr = value;
                break;
            case 0x3c3:
            case 0x46e8:
                /* enable VGA mode */
                if ( value == 0x01 )
                {
                    ET4000enabled = true;
                    //printf ( "ET4000 Enable VGA SubSystem\n" );
                }

                else
                {
                    ET4000enabled = false;
                    //printf ( "ET4000 Disable VGA SubSystem 0x%X\n", value );
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
                    xcb->user_palette [xcb->palette_ix++] = value;

                //xcb->palette_ix += 1;

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
                    //printf ( "6845 VSCONF1 16 bit display memory r/w %s\n", value & 0x40 ? "enabled" : "disabled" );
                    //printf ( "6845 VSCONF1 16 bit IO r/w %s\n", value & 0x80 ? "enabled" : "disabled" );
                    xcb->VSCONF1r = value;
                }

                if ( xcb->crtc_ix == VSCONF2 && xcb->KEY )
                {
                    //printf ( "6845 VSCONF2 Addressing Mode %s\n", value & 0x20 ? "TLI" : "IBM" );
                    xcb->VSCONF2r = value;
                }

                break;
            case 0x3d8: /* (RW) 6845 Display Mode Control Register colour */
                if ( value == 0xA0 )
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
                printf ( "ET4000 unknown write register 0x%X\n", a );
                break;
        }

        return 1;
    }

    /* must be a VRAM address */
    //printf ( "ET4000 VRAM write 0x%X, 0x%X\n", addr, value );
    offset = addr - NOVA_ET4000_VRAMBASE;

    if ( type == OP_TYPE_BYTE )
    {
        *( (uint8_t *)( RTGbuffer + offset ) ) = value;
    }

    else if ( type == OP_TYPE_WORD )
    {
        *( (uint16_t *)( RTGbuffer + offset) ) = htobe16 (value);
    }

    else if ( type == OP_TYPE_LONGWORD )
    {
        *( (uint32_t *)( RTGbuffer + offset ) ) = htobe32 (value);
    }

    return 1;
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

    sprintf ( command, "ffmpeg -vcodec rawvideo -f rawvideo -pix_fmt rgb565le -s %dx%d -i %s -f image2 -frames 1 -hide_banner -y -loglevel quiet -vcodec png screendump.png", w, h, dumpfile );
    system ( "cat /dev/fb0 > screendump.raw" );
    system ( command );
    system ( "bash ./screendump.sh" );

    return;
}


void logo ( void )
{
    uint16_t *dstptr; 
    uint32_t buff;
    uint32_t lineLength;
    uint32_t x, y;
    FILE *fp;
    char *logofile = "configs/AtariLogo800x600.bin";
    

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
    //printf ( "fbp = %p, screensize = %d\n", fbp, screensize );

    for ( int pixel = 0; pixel < screensize; pixel++ )
    {  
        fread ( &buff, 3, 1, fp );     

        y      = pixel / lineLength;
        x      = pixel % lineLength;
        
        dstptr [x + (y * lineLength)] = buff;
    }
  
    fclose ( fp );
}