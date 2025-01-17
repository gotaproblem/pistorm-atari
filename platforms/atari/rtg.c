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
#include "../../config_file/config_file.h"
#include <stdbool.h>
#include "et4000.h"


extern int RTG_fps;


// 'global' variables to store screen info
void *RTGbuffer = NULL;
void *VRAMbuffer = NULL;
int fbfd;
void *fbp;
struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
size_t screensize;
void *fbptr;
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

uint16_t palette4 [4] = {
   WHITE, RED, LT_GREEN, BLACK
};

uint16_t palette16 [16] = {
    WHITE, RED, LT_GREEN, YELLOW,
    BLUE, MAGENTA, CYAN, LT_GREY,
    GREY, LT_RED, LT_GREEN, LT_YELLOW,         
    LT_BLUE, LT_MAGENTA, LT_CYAN, BLACK };
       


void rtgInit ( void )
{
    char func [] = "[RTG]";


    // Open the file for reading and writing
    fbfd = open ( "/dev/fb0", O_RDWR );

    if ( !fbfd ) 
    {
        printf ( "%s Framebuffer failed to open\n", func );
        return;
    }

    printf ( "%s Framebuffer open\n", func );

    fbp = NULL;    

   if ( RTG_fps )
   {
        if ( RTG_fps > 75 )
            RTG_fps = 75;
            
        FRAME_RATE = (float)( 1.0 / RTG_fps ) * 1000000.0;
   }

    else 
    {
        RTG_fps = 60 ;
        FRAME_RATE = HZ60;
    }

    printf ( "%s FPS %d\n", func, RTG_fps );

    /* this might be a re-initialise, so don't allocate more memory */
    //if ( RTGbuffer == (void *)NULL )
    //{
        VRAMbuffer = malloc ( MAX_VRAM * 2 );
        RTGbuffer = malloc ( MAX_VRAM * 2 ); /* allocate max size */
        //RTGbuffer = mmap ( NULL, MAX_VRAM * 2, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0 );

        //if ( RTGbuffer == NULL )
        //if ( RTGbuffer == MAP_FAILED || VRAMbuffer == (void *)NULL )
        if ( RTGbuffer == (void *)NULL || VRAMbuffer == (void *)NULL )
        {
            printf ( "[RTG] Initialisation failed - %d\n", errno );

            return;
        }
    //}

    logo ();

    screensize = 0;
    fbptr = (void *)NULL;
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