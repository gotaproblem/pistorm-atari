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

#ifdef RAYLIB
#include <pthread.h>
#include <signal.h>
#endif

//#include <signal.h>
//#include <sys/time.h>
#include "../../config_file/config_file.h"

#ifdef RAYLIB
#include "raylib_pi4_test/raylib.h"
#endif

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

#ifndef RAYLIB
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
#endif

#define ATARI_VRAM_BASE   0x003f8000 /* 4MB machine - will get changed at detection */
#define VRAM ((640 * 400) / 8)    /* 32 KB 640 * 400 = 8 pixels per word */

#define LOW_RES 0
#define MED_RES 1
#define HI_RES  2

#define RGBalpha 0x0000 

#ifndef RAYLIB
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
    volatile uint16_t *sptr =(volatile uint16_t *)screen;

    
    if ( res == HI_RES )
    {
        ABPP = 1;
        X_SCALE = 2;
        Y_SCALE = 1;
    }

    else if ( res == MED_RES )
    {
        ABPP = 2;
        X_SCALE = 2;
        Y_SCALE = 2;
    }

    else if ( res == LOW_RES )
    {
        ABPP = 4;
        X_SCALE = 4;
        Y_SCALE = 2;
    }

    /* VRAM = 32 KB so divide by 2 for 16 bit WORDS */
    for ( uint32_t address = 0, pixel = 0; address < VRAM / 2; address += ABPP ) 
    {
        if ( res == HI_RES )
        {
            y      = pixel / 640;
            zoomY  = y * Y_SCALE;
            startingX = fbptr + (pixel % 640) * X_SCALE + (zoomY * finfo.line_length);

            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                x      = pixel % 640;
                y      = pixel / 640;
                zoomY  = y * Y_SCALE;
                zoomX  = x * X_SCALE;
                drawXY = fbptr + zoomX + (zoomY * finfo.line_length); 

                for ( int X = 0; X < X_SCALE; X++ )
                    *( drawXY + X ) = plane0 == 1 ? BLACK : WHITE; 

                pixel++;
            }    

            if ( Y_SCALE > 1 )
                memcpy ( startingX + 1 + finfo.line_length, startingX, 640 * (Y_SCALE - 1) );
        }

        if ( res == MED_RES )
        {
            y      = pixel / 640;
            zoomY  = y * Y_SCALE;
            startingX = fbptr + (pixel % 640) * X_SCALE + (zoomY * finfo.line_length);

            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                plane1 = ( be16toh ( sptr [ address + 1 ] ) >> ( 15 - n ) ) & 1; 
                x      = pixel % 640;
                zoomX  = x * X_SCALE;
                drawXY = fbptr + zoomX + (zoomY * finfo.line_length); 

                for ( int X = 0; X < X_SCALE; X++ )
                    *( drawXY + X ) = RTG_PALETTE_REG [plane1 << 1 | plane0]; 

                pixel++; 
            }

            if ( Y_SCALE > 1 )
                memcpy ( startingX + 1 + finfo.line_length, startingX, 640 * (Y_SCALE - 1) );
        }

        else if ( res == LOW_RES )
        {
            y      = pixel / 320;
            zoomY  = y * Y_SCALE;
            startingX = fbptr + (pixel % 320) * X_SCALE + (zoomY * finfo.line_length);

            for ( int n = 0; n < 16; n++ )
            {
                plane0 = ( be16toh ( sptr [ address ] ) >> ( 15 - n ) ) & 1; 
                plane1 = ( be16toh ( sptr [ address + 1 ] ) >> ( 15 - n ) ) & 1; 
                plane2 = ( be16toh ( sptr [ address + 2 ] ) >> ( 15 - n ) ) & 1; 
                plane3 = ( be16toh ( sptr [ address + 3 ] ) >> ( 15 - n ) ) & 1; 
                x      = pixel % 320;
                zoomX  = x * X_SCALE;
                drawXY = fbptr + zoomX + (zoomY * finfo.line_length); 
              
                for ( int X = 0; X < X_SCALE; X++ )
                    *( drawXY + X ) = RTG_PALETTE_REG [plane3 << 3 | plane2 << 2 | plane1 << 1 | plane0]; 

                pixel++;
            }  

            if ( Y_SCALE > 1 )
                memcpy ( startingX + 1 + finfo.line_length, startingX, 320 * (Y_SCALE - 1) );
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
#if (0)
                vinfo.xres = 1280;
                vinfo.yres = 800;
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
                *(uint16_t *)( ( fbptr + i ) ) = 0x39e7; //0x7bef;
            }

            RTGresChanged = 0;
        }

        /* draw the screen */
        draw ( thisRES );

        /* 50 Hz 20000 (20ms), 60 Hz 16666 (16.6ms), 70 Hz 14285 (14.2ms) */
        wait = FRAME_RATE;

        /* delay for required time or until resolution change has been signalled */
        while ( wait > 0 && RTGresChanged == 0 )
        {
            usleep ( 100 );

            wait -= 100;
        }
    }
}
#endif


#ifdef RAYLIB
extern struct emulator_config *cfg;
extern int get_named_mapped_item ( struct emulator_config *cfg, char *name );

void *rtgRender ( void* vptr )
{
    sigset_t set;

    sigemptyset ( &set );
    sigaddset ( &set, SIGINT );
    pthread_sigmask ( SIG_BLOCK, &set, NULL );

    /*
     * VGA  640x480
     * SVGA 800x600
     * XGA  1024x768
     * SXGA 1280x1024
     * UXGA 1600x1200
     * FHD  1920x1080
     * 
     */
    const int windowWidth = 640;
    const int windowHeight = 480;

    // Enable config flags for resizable window and vertical synchro
    //SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(windowWidth, windowHeight, "raylib [core] example - window scale letterbox");
    //SetWindowMinSize(320, 240);

    //int gameScreenWidth = 640;
    //int gameScreenHeight = 480;

    // Render texture initialization, used to hold the rendering result so we can easily resize it
    RenderTexture2D target = LoadRenderTexture ( windowWidth, windowHeight );
    //SetTextureFilter(target.texture, TEXTURE_FILTER_BILINEAR);  // Texture scale filter to use

    SetTargetFPS(60);                   // Set our game to run at 60 frames-per-second
    //--------------------------------------------------------------------------------------

    int ix = get_named_mapped_item ( cfg, "RTG" );
	uint16_t *src = (uint16_t *)( cfg->map_data [ix] );
	uint16_t *dst;
    Texture raylib_texture;
	Image raylib_fb;
    uint16_t *srcptr;
    uint16_t *dstptr;

	raylib_fb.format = PIXELFORMAT_UNCOMPRESSED_R5G6B5;
	raylib_fb.width = windowWidth;
	raylib_fb.height = windowHeight;
	raylib_fb.mipmaps = 1;
	raylib_fb.data = src;

    dst = malloc ( raylib_fb.width * raylib_fb.height * 2 );

	raylib_texture = LoadTextureFromImage ( raylib_fb );

    // Detect window close button or ESC key
    while ( !WindowShouldClose () && cpu_emulation_running )        
    {
        srcptr = src;
        dstptr = dst;
    
	    for( unsigned long l = 0 ; l < ( raylib_fb.width * raylib_fb.height ) ; l++ )
		    *dstptr++ = be16toh ( *srcptr++ );

        UpdateTexture ( raylib_texture, dst );

        BeginDrawing ();
		DrawTexture ( raylib_texture, 0, 0, WHITE );
        EndDrawing ();
        //BeginTextureMode ( target );
        //EndTextureMode ();
    }

    // De-Initialization
    //--------------------------------------------------------------------------------------
    UnloadRenderTexture ( target );        // Unload render texture

    CloseWindow ();                      // Close window and OpenGL context
    //--------------------------------------------------------------------------------------

    cpu_emulation_running = 0;
    
    printf ( "[RTG] thread terminated\n" );
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
#endif

#ifndef RAYLIB
void rtg ( int size, uint32_t address, uint32_t data ) 
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
#endif


void rtgInit ( void )
{
    char func [] = "[RTG] ";

    /* check .cfg has setvar rtg */
    if ( !RTG_enabled )
        return;
#ifndef RAYLIB
    // Open the file for reading and writing
    fbfd = open ( "/dev/fb0", O_RDWR );

    if ( !fbfd ) 
    {
        printf ( "%sFATAL: cannot open framebuffer device.\n", func );
        return;
    }

    printf ( "%sFramebuffer device open\n", func );

    // Get variable screen information
    if ( ioctl ( fbfd, FBIOGET_VSCREENINFO, &vinfo ) ) 
    {
        printf("%sError reading variable information.\n", func );
        return;
    }

    //printf ( "%sHDMI resolution %dx%d, %dbpp\n", func, vinfo.xres, vinfo.yres, vinfo.bits_per_pixel );

    /* /boot/config.txt - do not configure hdmi - leave to auto detect */
    /* need xres and yres doubled to handle HI_RES for some reason */
    vinfo.xres = 1280;
    vinfo.yres = 800;
    vinfo.xres_virtual = 640;
    vinfo.yres_virtual = 400;
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

    fbptr = (uint16_t *)fbp;

    //printf ( "screen RAM size is 0x%X\n", screensize );
    //printf ( "screen line length is %d\n", finfo.line_length );
#endif

    if ( ( screen = (volatile uint8_t *)calloc ( (640 * 400) / 8, 1 ) ) == NULL )
    {
        printf ( "%sFATAL - failed to allocate memory to screen buffer\n", func );
        return;
    }

    RTG_ATARI_SCREEN_RAM = ATARI_VRAM_BASE;
    RTG_RES = LOW_RES;
    RTGresChanged = 1;
}


