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
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "et4000.h"
#include "../../config_file/config_file.h"
#include "../../raylib-test/raylib.h"
#include <pthread.h>
//#include <endian.h>
#include <sys/mman.h>


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

static volatile nova_xcb_t nova_xcb;
static volatile nova_xcb_t *xcb;
static bool first;
volatile bool ET4000enabled = false;
bool RTG_enabled;
bool RTG_initialised;
volatile bool RTG_hold;
volatile bool RTG_updated;

int ix;
//int ir [20];
/*
enum 
{
    AWHITE      = 0xffff,
    ARED        = 0xf800,
    AGREEN      = 0x07e0,
    AYELLOW     = 0xffe0,
    ABLUE       = 0x001f,
    AMAGENTA    = 0xf81f,
    ACYAN       = 0x07ff,
    ALT_GREY    = 0x7bcf,
    AGREY       = 0x5acb,
    ALT_RED     = 0xfacb,
    ALT_GREEN   = 0x5feb,
    ALT_YELLOW  = 0xffef,
    ALT_BLUE    = 0x5fff,
    ALT_MAGENTA = 0xf897,
    ALT_CYAN    = 0xdfff,
    ABLACK      = 0

} palette;
*/

enum
{
    E_BLACK         = 0,
    E_BLUE          = 137, 
    E_GREEN         = 133, 
    E_CYAN          = 139, 
    E_RED           = 128, 
    E_MAGENTA       = 140, 
    E_BROWN         = 34, 
    E_WHITE         = 207,
    E_GREY          = 143, 
    E_LT_BLUE       = 183, 
    E_LT_GREEN      = 213, 
    E_LT_CYAN       = 219,
    E_LT_RED        = 208, 
    E_LT_MAGENTA    = 220,
    E_YELLOW        = 210, 
    E_INTENSE_WHITE = 255

} ET4000_palette;
/*
uint16_t palette16 [16] = {
    AWHITE, ARED, ALT_GREEN, AYELLOW,
    ABLUE, AMAGENTA, ACYAN, ALT_GREY,
    AGREY, ALT_RED, ALT_GREEN, ALT_YELLOW,         
    ALT_BLUE, ALT_MAGENTA, ALT_CYAN, ABLACK 
};
*/
uint8_t RTG_PALETTE_REG [16] =
{
    E_BLACK, E_BLUE, E_GREEN, E_CYAN, 
    E_RED, E_MAGENTA, E_BROWN, E_WHITE,
    E_GREY, E_LT_BLUE, E_LT_GREEN, E_LT_CYAN,
    E_LT_RED, E_LT_MAGENTA, E_YELLOW, E_INTENSE_WHITE
}; /* palette colours */

extern struct emulator_config *cfg;
extern int get_named_mapped_item ( struct emulator_config *cfg, char *name );
extern void cpu2 ( void );
extern volatile int g_irq;
extern volatile int g_iack;
//extern volatile uint32_t last_irq;
extern volatile int cpu_emulation_running;
extern volatile int g_buserr;
volatile uint32_t RTG_VSYNC;

//volatile bool ResolutionChanged;
volatile int RESOLUTION = 0;
RenderTexture2D target;

#define MAX_WIDTH 1024
#define MAX_HEIGHT 768

#define GETRES() (uint16_t)( ((xcb->crtc_index [0x35] & 0x02) >> 1 ) << 10 | ((xcb->crtc_index [7] & 0x20) >> 5) << 9 | (xcb->crtc_index [7] & 0x01) << 8 | xcb->crtc_index [6] )

/*
 * CGA
 * EGA
 * VGA  640x480
 * SVGA 800x600
 * XGA  1024x768
 * SXGA 1280x1024
 * UXGA 1600x1200
 * FHD  1920x1080
 * 
 */
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

void *dst = NULL;
void *RTGbuffer = NULL;
void *lbuffer = NULL;

void draw ( void )
{
    int      windowWidth;
    int      windowHeight;
    //void     *dst = NULL;
    void     *src = NULL;
    //void     *mapping = NULL;
    Texture  raylib_texture;
	Image    raylib_fb;
    uint8_t  plane0, plane1, plane2, plane3;
    int      ABPP;
    uint32_t address;
    uint32_t pixel;
    int      SCREEN_SIZE;
    int      ix;
    static   Rectangle srcrect, dstscale;
    static   Vector2 origin;


    //static int delay = 1800 * 1;
    
    ix = get_named_mapped_item ( cfg, "ET4000vram" );

    if ( ix == -1 )
    {
        printf ( "[RTG] emulator configuration file is mis-configured for RTG\n" );

        cpu_emulation_running = 0;

        return;
    }

    raylib_fb.data = NULL;
    //mapping = (void*)cfg->map_data [ix];
    //src = RTGbuffer;

reinit:
    /* RESOLUTION is set to zero in the et4000write () */
    while ( RESOLUTION == 0 )//&& !WindowShouldClose () )
    {
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

            /*
             * **************************
             * check for Linear or Planar
             * ************************** 
             */

            /* Chain 4 (Linear) */
            if ( (xcb->ts_index [4] & 0x08) == 0x08 && xcb->ts_index [2] == 0x0F )
            {
                /* res 256 colours */
                if ( xcb->ts_index [7] == 0xF4 || ( GETRES () == 803 && xcb->ts_index [7] == 0xB4 ) ) 
                {
                    //dst              = (uint16_t *)malloc ( windowWidth * windowHeight * sizeof (uint16_t) );

                    raylib_fb.format = PIXELFORMAT_UNCOMPRESSED_R5G6B5;

                    RESOLUTION       = 2; 
                    //SCREEN_SIZE      = windowWidth * windowHeight * 2;
                }
                
                /* res 32K/64K colours */
                else
                {
                    //dst              = (uint16_t *)malloc ( windowWidth * windowHeight * sizeof (uint16_t) );

                    raylib_fb.format = PIXELFORMAT_UNCOMPRESSED_R5G6B5;

                    RESOLUTION       = 4;
                    //SCREEN_SIZE      = windowWidth * windowHeight * 2;
                }
            }

            /* Planar */
            else if ( (xcb->ts_index [4] & 0x08) == 0x00 )
            {
                /* Monochrome */
                if ( xcb->ts_index [2] == 0x01 ) // plane mask
                {
                    //dst              = (uint8_t *)malloc ( windowWidth * windowHeight * sizeof (uint8_t) ); 
                    
                    raylib_fb.format = PIXELFORMAT_UNCOMPRESSED_GRAYSCALE;
                    
                    RESOLUTION       = 1;
                    //SCREEN_SIZE          = windowWidth * windowHeight;
                }

                /* Colour */
                else if ( xcb->ts_index [2] == 0x0F ) // plane mask
                {
                    //dst              = (uint16_t *)malloc ( windowWidth * windowHeight * sizeof (uint16_t) ); 
                    
                    raylib_fb.format = PIXELFORMAT_UNCOMPRESSED_R5G6B5;
                    
                    RESOLUTION       = 3;
                    //SCREEN_SIZE      = windowWidth * windowHeight * 2;
                }

                else 
                {
                   printf ( "Planer - plane mask 0x%X\n", xcb->ts_index [2] );
                }
            }

            /* check to make sure minimal configuration is done */
            if ( RESOLUTION && xcb->ts_index [2] )
            {
                raylib_fb.data       = dst;
                raylib_fb.width      = windowWidth;
                raylib_fb.height     = windowHeight;
                raylib_fb.mipmaps    = 1;              
                raylib_texture       = LoadTextureFromImage ( raylib_fb );

                //SetTextureWrap ( raylib_texture, TEXTURE_WRAP_CLAMP );

                SCREEN_SIZE          = windowWidth * windowHeight;
            }
        }
    }

    printf ( "\nRESOLUTION = %d\n\n", RESOLUTION );

    srcrect.x = 0;
    srcrect.y = 0;
    srcrect.width = windowWidth;
    srcrect.height = windowHeight;

    dstscale.width = windowWidth;
    dstscale.height = windowHeight;

    origin.x = (dstscale.width - MAX_WIDTH) * 0.5;
    origin.y = (dstscale.height - MAX_HEIGHT) * 0.5;
    
    while ( !WindowShouldClose () && RESOLUTION )     
    {        
        if ( !g_irq )
        {
        BeginDrawing ();
        RTG_updated = false;

        memcpy ( lbuffer, RTGbuffer, SCREEN_SIZE * 2 ); // bytes or words?
        src = lbuffer;

        /* Monochrome */
        if ( RESOLUTION == 1 )
        {
            ABPP = 1;
            uint8_t *dptr = dst;
            uint8_t *sptr = src;
            
            for ( uint32_t address = 0, pixel = 0; pixel < SCREEN_SIZE; address++ ) 
            {
                for ( int ppb = 0; ppb < 8 / ABPP; ppb++, pixel++ ) /* pixels per byte eg. 4bpp = 2 */
                    *( dptr + pixel ) = ( *( sptr + address ) >> ( 7 - ppb ) ) & 0x1 ? 0x20 : 255;//0xa0;
            }
        }

        /* Colour 8bit 256 colours */
        else if ( RESOLUTION == 2 )
        {
            uint16_t *dptr = dst;
            uint8_t  *sptr = src;
            uint16_t ix;
            uint8_t  r, g, b;

            for ( address = 0, pixel = 0; pixel < SCREEN_SIZE; pixel++, address++ ) 
            {
                //if ( !g_irq )
                // {
                ix = sptr [address] * 3;                // pointer to palette index
                
                r  = xcb->user_palette [ix++] >> 1;     // 6 bit red to 5 bit
                g  = xcb->user_palette [ix++];          // 6 bit green 
                b  = xcb->user_palette [ix] >> 1;       // 6 bit blue t 5 bit

                dptr [pixel] = r << 11 | g << 5 | b;
                //}
            }
        }

        /* Colour 4bit 16 colours */
        else if ( RESOLUTION == 3 )
        {
            uint16_t *dptr = dst;
            uint16_t *sptr = src;
            ABPP = 4;
            uint32_t colour;
            uint8_t r, g, b;
            int plane0, plane1, plane2, plane3;
            
            for ( uint32_t address = 0, pixel = 0; pixel < SCREEN_SIZE; address += 1 ) 
            {
                for ( int ppb = 0; ppb < 16; ppb++, pixel++ )
                {
                    plane0 = xcb->ts_index [2] & 0x01 ? ( be16toh ( sptr [address] ) >> ( 15 - ppb ) ) & 0x1 : 0; // Blue      
                    plane1 = xcb->ts_index [2] & 0x02 ? ( be16toh ( sptr [address + 1] ) >> ( 15 - ppb ) ) & 0x1 : 0; // Green     
                    plane2 = xcb->ts_index [2] & 0x04 ? ( be16toh ( sptr [address + 2] ) >> ( 15 - ppb ) ) & 0x1 : 0; // Red       
                    plane3 = xcb->ts_index [2] & 0x08 ? ( be16toh ( sptr [address + 3] ) >> ( 15 - ppb ) ) & 0x1 : 0; // Intensity 

                    colour = vga_palette [plane3 << 3 | plane2 << 2 | plane1 << 1 | plane0];
                    r = ((colour >> 16) & 0xff) >> 3;
                    g = ((colour >> 8) & 0xff) >> 2;
                    b = (colour & 0xff) >> 3;
                    
                    dptr [pixel] = r << 11 | g << 5 | b;
                }
                //printf ( "SCREEN SIZE %d - pixel %d, address %d\n", SCREEN_SIZE, pixel, address );
            }
        }
        
        /* Colour 16bit 64k colours */
        else if ( RESOLUTION == 4 )
        {
            uint16_t *dptr = dst;
            uint16_t *sptr = src;

            for ( address = 0, pixel = 0; pixel < SCREEN_SIZE; pixel++, address++ ) 
                    dptr [pixel] = sptr [address];
        }
        
        SetTextureFilter ( raylib_texture, 1 );

       // if ( !g_irq )
       // {
        //BeginDrawing ();
           
            RTG_VSYNC = 0; /* not in vblank */
            //RTG_updated = false;

            ClearBackground ( RAYWHITE );

            UpdateTexture ( raylib_texture, dst );
        
            //DrawTexture ( raylib_texture, (MAX_WIDTH - windowWidth) / 2, (MAX_HEIGHT - windowHeight) / 2, RAYWHITE );
            DrawTexturePro ( raylib_texture, srcrect, dstscale, origin, 0.0f, RAYWHITE );
            
            DrawFPS ( (MAX_WIDTH - windowWidth) / 2 + (windowWidth - 90), (MAX_HEIGHT - windowHeight) / 2 );

            RTG_updated = true;
            
        EndDrawing ();
        }
        RTG_VSYNC = 1; /* in vblank */
    }

    printf ( "ET4000 Changing RESOLUTION\n" );

    UnloadTexture ( raylib_texture );

    //free ( (void*)dst );

    UnloadRenderTexture ( target );
    //CloseWindow ();

    if ( RESOLUTION == 0 )
        goto reinit;

    cpu_emulation_running = 0;
}


void *rtgRender ( void *vptr )
{
    const struct sched_param priority = {99};

    usleep ( 1000000 );

    //cpu2 ();
    //sched_setscheduler ( 0, SCHED_FIFO, &priority );
    //system ( "echo -1 >/proc/sys/kernel/sched_rt_runtime_us" );

    while ( !cpu_emulation_running )
        ;

    InitWindow ( MAX_WIDTH, MAX_HEIGHT, "PiStorm Atari RTG" );
    target = LoadRenderTexture ( MAX_WIDTH, MAX_HEIGHT );
    SetTextureFilter ( target.texture, TEXTURE_FILTER_BILINEAR);
    SetTargetFPS ( 60 );

    usleep ( 100000 );

    //RenderTexture2D target = LoadRenderTexture ( MAX_WIDTH, MAX_HEIGHT );
    //SetTextureFilter ( target.texture, TEXTURE_FILTER_BILINEAR);

    RESOLUTION = 0;

    while ( cpu_emulation_running )
    {
        if ( ET4000enabled )            
            draw ();
    }

    UnloadRenderTexture ( target );
    CloseWindow ();
    
    //if ( retry <= 0 )
    //    printf ( "[RTG] ET4000 was not enabled\n" );

    cpu_emulation_running = 0;

    printf ( "[RTG] thread terminated\n" );
}


int et4000Init ( void )
{
    xcb = &nova_xcb;
    printf ( "[RTG] ET4000 starting\n" );

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
    /*
    buffer = mmap ( NULL, 
        MAX_WIDTH * MAX_HEIGHT * 2, 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED | MAP_ANONYMOUS, 
        -1, 
        0 );

    dst = mmap ( NULL, 
        MAX_WIDTH * MAX_HEIGHT * 2, 
        PROT_READ | PROT_WRITE, 
        MAP_SHARED | MAP_ANONYMOUS, 
        -1, 
        0 );
    */

    RTGbuffer = malloc ( MAX_WIDTH * MAX_HEIGHT * 2 );
    lbuffer   = malloc ( MAX_WIDTH * MAX_HEIGHT * 2 );
    dst       = malloc ( MAX_WIDTH * MAX_HEIGHT * 2 );

    RTG_initialised = true;
    RTG_VSYNC = 1;
    RTG_updated = true;

    //if ( dst == MAP_FAILED || buffer == MAP_FAILED ) 
    if ( dst == NULL || RTGbuffer == NULL ) 
    {
        printf ( "[RTG] ET4000 Initialisation failed\n" );
        RTG_initialised = false;
    }
#ifdef TRYDMA
    //virt_dma_regs = map_segment ( (void *)DMA_BASE, 0x1000 );
    virt_dma_regs = (void *) gpio;
    enable_dma ();
#endif
    return 1;
}


uint32_t et4000read ( uint32_t addr, uint32_t *value, int type )
{
    //printf ( "ET4000 reg read 0x%X\n", addr );
    if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGBASE + 0x400 )
    {
        *value = 1;

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
                /* if emulator cnf file has not set setenv rtg then do not enable ET4000 */
                if ( !ET4000enabled && !RTG_enabled )
                {
                    *value = 0xff;
                    g_buserr = 1; 
                }

                else
                    *value = xcb->videoSubsystemr;

                /* stop emutos initialising ET4000 */
                if ( first )
                {
                    //*value = 0xff;
                    first = false;
                    //g_buserr = 1; /* uncomment this line if you don't want EMUtos to init ET4000 */
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
                //*value = (~RTG_VSYNC << 7) | (RTG_VSYNC << 3) | ET4000enabled;
                *value = ET4000enabled;
                xcb->atc_ixdff = false; /* reset index/data flip-flop */
                break;

            default:
                printf ( "ET4000 unknown read register 0x%X\n", a );
                break;
        }

        return 0;
    }

    else if ( addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_VRAMTOP )
    {
        if ( type == OP_TYPE_BYTE )
            return *( uint8_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) );

        else if ( type == OP_TYPE_WORD )
            return be16toh ( *( uint16_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) ) );

        else if ( type == OP_TYPE_LONGWORD )
            return be32toh ( *(uint32_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) ) );
    }
}


uint32_t et4000write ( uint32_t addr, uint32_t value, int type )
{
    //printf ( "ET4000 reg write 0x%X, 0x%X\n", addr, value );

    if ( addr >= NOVA_ET4000_REGBASE && addr < NOVA_ET4000_REGBASE + 0x400 )
    {
        uint32_t a = addr - NOVA_ET4000_REGBASE;

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

                if ( xcb->ts_ix == 2 )
                    printf ( "TS Write Plane Mask = 0x%X\n", value );

                else if ( xcb->ts_ix == 4 )
                {
                    //if ( (xcb->ts_index [xcb->ts_ix] & 0x08) != (value & 0x08) )
                    //{
                        printf ( "Resolution changed\n" );
                        //ResolutionChanged = true;
                        RESOLUTION = 0;
                    //}
                
                    printf ( "TS Memory Mode = 0x%X - Map mask = %d, %s mode enabled\n", value, value & 0x04, value & 0x08 ? "Chain 4 (Linear)" : "Planer" );
                }

                else if ( xcb->ts_ix == 7 )
                {
                    printf ( "TS Auxillary Mode = 0x%X - %s mode enabled\n", value, value & 0x80 ? "VGA" : "EGA" );
                    xcb->VGAmode = value & 0x80;
                }

                xcb->ts_index [xcb->ts_ix] = value;

                break;
            case 0x3c2: /* GENERAL register - Miscellaneous Output */
                printf ( "MISC_W using CRTC addresses %s\n",
                    value & 0x01 ? "3Dx Colour" : "3Bx Monochrome" );
                printf ( "MISC_W DMA access %s\n",
                    value & 0x02 ? "enabled" : "disabled" );
                printf ( "MISC_W MCLK = 0x%X\n", value & 0x0c );

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
                    printf ( "6845 VSCONF1 Addressing Mode %s\n", value & 0x20 ? "TLI" : "IBM" );
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

    else if ( addr >= NOVA_ET4000_VRAMBASE && addr < NOVA_ET4000_VRAMTOP )
    {
        if ( type == OP_TYPE_BYTE )
            *( (uint8_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) ) ) = value;

        else if ( type == OP_TYPE_WORD )
            *( (uint16_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) ) ) = htobe16 (value);

        else if ( type == OP_TYPE_LONGWORD )
            *( (uint32_t *)( RTGbuffer + (addr - NOVA_ET4000_VRAMBASE) ) ) = htobe32 (value);

        return 1;
    }
}
