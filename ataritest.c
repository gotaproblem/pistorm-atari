/*
 *
 * syntax
 * ataritest --peek address=0xff8200 loop=yes
 * ataritest --poke address=0x600 data=0x33
 * ataritest --clearmem size=16536 pattern=0x1234
 * ataritest --dumprom address=0xe00000 size=192 or size=256
 * ataritest --init 512 or 1024 or 2048 or 4096
 * ataritest --memory tests=rwx size=512 loop=yes
 * 
 * 
 */

#include <assert.h>
#include <dirent.h>
#include <endian.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include "emulator.h"
#include "gpio/ps_protocol.h"

#define SIZE_KILO 1024
#define SIZE_MEGA (1024 * 1024)
#define SIZE_GIGA (1024 * 1024 * 1024)

#define OFFSET          0x0600
#define MEM_READ        0
#define MEM_WRITE       1
#define TEST_8          8
#define TEST_8_ODD      81
#define TEST_8_RANDOM   82
#define TEST_16         16
#define TEST_16_ODD     161
#define TEST_16_RANDOM  162
#define TEST_32         32
#define TEST_32_ODD     321
#define TEST_32_RANDOM  322

#define REVERSE_VIDEO   "\033[7m"
#define NORMAL          "\033[0m"


int  parser ( int argc, char **argv );
int  memTest ( int direction, int type, uint32_t startAdd, uint32_t length, uint8_t *garbagePtr );
void clearmem ( uint32_t length, uint32_t *duration, uint16_t pattern );
void setMemory ( uint32_t size );
void peek ( uint32_t start );
void poke ( uint32_t address, uint8_t data );
void dump ( uint32_t ROMsize, uint32_t ROMaddress );
void memspeed ( uint32_t length );
void hwTest ( void );

int doReads;
int doWrites;
int doRandoms;
uint32_t testSize;
uint32_t memSize;
int totalErrors;
int loopTests;
int errorStop;
uint32_t padd;
uint8_t pdata;
int cmdPeek = 0;
int cmdPoke = 0;
int cmdMem = 0;
int cmdDump = 0;
int cmdClear = 0;
int cmdInit = 0;
int cmdMemSpeed = 0;
int targetF = 200;
int cmdHWTEST = 0;
uint32_t ROMsize = 192;
uint32_t ROMaddress = 0x00e00000;
uint16_t clrPattern = 0x0000;


uint8_t *garbege_datas;
extern volatile unsigned int *gpio;
extern uint8_t fc;

struct timespec f2;

uint32_t mem_fd;
uint32_t errors = 0;
uint8_t  loop_tests = 0;
uint32_t cur_loop;



void sigint_handler(int sig_num)
{
  printf ( "\nATARITEST aborted\n\n");

  if (mem_fd)
    close(mem_fd);

  exit(0);
}


void ps_reinit() 
{
    ps_reset_state_machine();
    ps_pulse_reset();

    usleep(1500);
}


int check_emulator() 
{

    DIR* dir;
    struct dirent* ent;
    char buf[512];

    long  pid;
    char pname[100] = {0,};
    char state;
    FILE *fp=NULL;
    const char *name = "emulator";

    if (!(dir = opendir("/proc"))) {
        perror("can't open /proc, assuming emulator running");
        return 1;
    }

    while((ent = readdir(dir)) != NULL) {
        long lpid = atol(ent->d_name);
        if(lpid < 0)
            continue;
        snprintf(buf, sizeof(buf), "/proc/%ld/stat", lpid);
        fp = fopen(buf, "r");

        if (fp) {
            if ( (fscanf(fp, "%ld (%[^)]) %c", &pid, pname, &state)) != 3 ){
                printf("fscanf failed, assuming emulator running\n");
                fclose(fp);
                closedir(dir);
                return 1;
            }
            if (!strcmp(pname, name)) {
                fclose(fp);
                closedir(dir);
                return 1;
            }
            fclose(fp);
        }
    }

    closedir(dir);
    return 0;
}


int main(int argc, char *argv[]) 
{
    uint32_t test_size = 2 * SIZE_KILO;
    uint32_t add;


    cur_loop = 1;

    if ( check_emulator () ) 
    {
        printf("PiStorm emulator running, please stop this before running ataritest\n");
        return 1;
    }

    clock_gettime ( CLOCK_PROCESS_CPUTIME_ID, &f2 );
    srand ( (unsigned int)f2.tv_nsec );

    signal ( SIGINT, sigint_handler );

    //ps_setup_protocol ( targetF );
    //exit(1);
   // ps_reset_state_machine ();
   // ps_pulse_reset ();

   // usleep (1500);

	fc = 6; //0b101;
    //write8( 0xff8001, 0b00001010 ); // memory config 512k bank 0
    
    doReads = 0;
    doWrites = 0;
    doRandoms = 0;
    memSize = 0;
    testSize = 512;
    totalErrors = 0;
    loopTests = 0;
    errorStop = 0;
    clrPattern = 0x0000;

   
    if ( parser ( argc, argv ) )
    {
        ps_setup_protocol ( targetF );
        ps_reset_state_machine ();
        ps_pulse_reset ();

        //printf ( "memory <%c%c%c> <%d> %s\n", 
        //    doReads ? 'r' : '-', doWrites ? 'w' : '-', doRandoms ? 'x' : '-', 
        //    testSize,
        //    loopTests ? "looping" : "" );
        //printf ( "Initialsed ATARI MMU for %d KB memory \n", memSize );
        if ( !memSize )
        {
            memSize = 512;
        }

        if ( testSize > memSize )
        {
            memSize = ( testSize <= 1024 ? 1024 : ( testSize <= 2048 ? 2048 : 4096) );
        }

        if ( cmdInit )
        {
            /* must be 512 or 1024 or 2048 or 4096 */
            setMemory ( memSize );
        }

        if ( cmdMem )
        {
            /* must be 512 or 1024 or 2048 or 4096 */
            setMemory ( memSize );

            printf ( "\nATARITEST\n" );
            printf ( "ATARI MMU configured to use %d KB\n", memSize );
        }

        if ( cmdClear )
        {
            uint32_t duration;

            /* must be 512 or 1024 or 2048 or 4096 */
            setMemory ( memSize );

            printf ( "\nClearing ATARI ST RAM - %d KB\n", testSize );
            fflush (stdout);

            clearmem ( testSize * SIZE_KILO, &duration, clrPattern );
            
            printf ( "\nATARI ST RAM cleared in %d ms @ %.2f MB/s\n\n", 
                //duration, ( (float)((testSize * 1024) - OFFSET) / (float)duration * 1000.0) / 1024 );
                duration, ( ( 1.0 / (float)duration ) * testSize ) );

        }

        if ( cmdPoke )
        {
            poke ( padd, pdata );
           // printf ("poking %x with %x\n", padd, pdata );
        }

        if ( cmdPeek )
        {
            if ( loopTests )
            {
                while ( loopTests )
                {
                    printf ( "\033[2J" );
                    peek ( padd );
                    usleep(50000);  /* 50ms delay to make redraw smoother */
                }
            }

            else
                peek ( padd );
        }

        if ( cmdDump )
        {
            printf ( "Dumping onboard ATARI ROM from 0x%X to file tos.rom\n", ROMaddress );

            dump ( ROMsize, ROMaddress );

            printf ( "ATARI ROM dumped - %d KB\n", ROMsize );
        }

        if ( cmdMemSpeed )
        {
            printf ( "\nChecking ATARI ST RAM memory bandwidth - %d KB\n", testSize );
            fflush (stdout);

            memspeed ( testSize * SIZE_KILO );
            
            //printf ( "\nATARI ST RAM cleared in %d ms @ %.2f KB/s\n\n", 
            //    duration, ( (float)((testSize * 1024) - OFFSET) / (float)duration * 1000.0) / 1024 );
        }

        if ( cmdMem )
        {
            if ( !doReads && !doWrites && !doRandoms )
            {
                printf ( "No memory tests selected\n" );

                exit (0);        
            }

            test_size = testSize * SIZE_KILO;
            

            garbege_datas = malloc ( test_size );

            if ( !garbege_datas )
            {
                printf ( "Failed to allocate memory for garbege datas\n" );

                return 1;
            }

            printf ( "Testing %d KB of memory - Starting address $%.6X\n", test_size / SIZE_KILO, OFFSET );

            if ( loopTests )
                printf ( "Test looping enabled\n" );

            //if (doWrites)
            {
                printf ( "Priming test data\n");

                add = (uint32_t)OFFSET;

                for ( uint32_t i = 0; add < test_size; i++, add++ ) 
                {
                    garbege_datas [i] = add % 2 ? (add - 1 >> 8) & 0xff : add & 0xff;

                    //if ( i == 0 )
                    //    printf ( "add %.8X = %.2X\n", add, garbege_datas [i] );
                    write8 ( add, garbege_datas [i] );
                }
            }

test_loop:

            if (doReads)
            {
                printf ( "\n%sTesting Reads...%s\n\n", REVERSE_VIDEO, NORMAL );

                if ( ! memTest ( MEM_READ,  TEST_8,         OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_READ,  TEST_16,        OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_READ,  TEST_16_ODD,    OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_READ,  TEST_32,        OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_READ,  TEST_32_ODD,    OFFSET, test_size, garbege_datas ) ) return 1;
            }

            if (doWrites)
            {
                printf ( "\n%sTesting Writes...%s\n\n", REVERSE_VIDEO, NORMAL );

                if ( ! memTest ( MEM_WRITE, TEST_8,         OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_WRITE, TEST_16,        OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_WRITE, TEST_16_ODD,    OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_WRITE, TEST_32,        OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_WRITE, TEST_32_ODD,    OFFSET, test_size, garbege_datas ) ) return 1;
            }

            if (doRandoms)
            {
                printf ( "\n%sTesting Random Reads / Writes...%s\n\n", REVERSE_VIDEO, NORMAL );

                if ( ! memTest ( MEM_READ,  TEST_8_RANDOM,  OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_READ,  TEST_16_RANDOM, OFFSET, test_size, garbege_datas ) ) return 1;
                if ( ! memTest ( MEM_READ,  TEST_32_RANDOM, OFFSET, test_size, garbege_datas ) ) return 1;
            }

            if (loopTests) 
            {
                printf ( "%-20s%sPass %d complete. ", "", REVERSE_VIDEO, cur_loop );
                printf ( "Total errors %d%s\n\n", totalErrors, NORMAL );

                sleep(1);

                printf ( "\n%-20s%sStarting pass %d%s\n", "", REVERSE_VIDEO, cur_loop + 1, NORMAL );                

                printf ( "Priming test data\n" );

                add = (uint32_t)OFFSET;

                for ( uint32_t i = 0; add < test_size; i++, add++ ) 
                {
                    garbege_datas [i] = (uint8_t)(rand() % 0xFF );
                    write8 ( add, (uint32_t) garbege_datas [i] );
                }

                cur_loop++;

                goto test_loop;
            }
        }
    
        if ( cmdHWTEST )
        {
            hwTest ();

        }
    }

    else
    {
        printf ( "ATARITEST syntax error\n"
                 "--clearmem <size=xxx> <pattern=xxxx>\n"
                 "     fills memory for specified size with 0's or pattern.\n"
                 "     <size> 512 to 4096. If not supplied, 512 is used.\n"
                 "     <pattern> memory will be filled with 16bit pattern\n"
                 "--memory tests=<rwx> <loop=yes> <stop=yes> <size=xxx>\n"
                 "     Run memory tests.\n"
                 "     tests r=read, w=write, x=random reads/writes.\n"
                 "     At least one test must be supplied.\n"
                 "     <loop> repeats tests until CNTRL-C is entered.\n"
                 "     <stop> aborts tests on an error.\n"
                 "--peek address=xxxxxx <loop=yes>\n"
                 "     examine address.\n"
                 "     displays 256 bytes from address.\n"
                 "     <loop> continuously reads 256 bytes from address.\n"
                 "--poke address=xxxxxx data=xx\n"
                 "     writes BYTE data to address.\n"
                 "--dumprom address=xxxxxx size=[192 | 256]\n"
                 "     reads memory from address to address+size and writes to file tos.rom\n"
                 "--init <size=xxx>\n"
                 "     configures ATARI MMU for the specified size.\n"
                 "     <size> 512 to 4096. If not supplied, 512 is used\n"
        );

        exit (0);
    }

    if ( cmdMem )
    {
        printf ( "\nATARITEST complete\n\n");
    }

    else
        printf ( "\n" );

    return 0;
}


void m68k_set_irq(unsigned int level) {
}


void memspeed ( uint32_t length )
{
    uint32_t address;
    struct timespec tmsStart, tmsEnd;
    long int nanoStart;
    long int nanoEnd;


    printf ( "Memory Speed Test\n" );

    /* READ */
    clock_gettime ( CLOCK_REALTIME, &tmsStart );

    for ( address =  0; address < length; address += 2 )
        read16 ( address );

    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

    nanoStart = (tmsStart.tv_sec * 1000) + (tmsStart.tv_nsec / 1000000);
    nanoEnd = (tmsEnd.tv_sec * 1000) + (tmsEnd.tv_nsec / 1000000);

    printf ( "READ:  %d ms = %.2f MB/s\n", (nanoEnd - nanoStart), 
        ( 1.0 / ( (float)(nanoEnd - nanoStart) ) * length ) / 1024 );     /* MB/s */


    /* WRITE */
    clock_gettime ( CLOCK_REALTIME, &tmsStart );
    
    for ( address = 0; address < length; address += 2 )
        write16 ( address, 0x5a5a);

    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

    nanoStart = (tmsStart.tv_sec * 1000) + (tmsStart.tv_nsec / 1000000);
    nanoEnd = (tmsEnd.tv_sec * 1000) + (tmsEnd.tv_nsec / 1000000);

    printf ( "WRITE: %d ms = %.2f MB/s\n", (nanoEnd - nanoStart), 
        ( 1.0 / ( (float)(nanoEnd - nanoStart) ) * length ) / 1024 );     /* MB/s */
}


void peek ( uint32_t start )
{
    char            ascii [17];
    int             i, j;
    uint32_t        address;
    unsigned char   data [0x100];                                   /* 256 byte block */
    int             size = 0x100;
    

        
    address = (start / 16) * 16;                                    /* we want a 16 byte boundary */
 
    ascii[16] = '\0';

    printf ( "\n Address    00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n\n" );

    for ( i = 0; i < size; ++i, address++ ) 
    {
        data [i] = read8 (address);

        if ( !(address % 16) )
            printf( " $%.6X  | ", address );

        printf( "%02X ", ((unsigned char*)data)[i]);

        if (((unsigned char*)data)[i] >= ' ' && ((unsigned char*)data)[i] <= '~') 
        {
            ascii[i % 16] = ((unsigned char*)data)[i];
        } 

        else 
        {
            ascii[i % 16] = '.';
        }

        if ((i+1) % 8 == 0 || i+1 == size) 
        {
            //printf(" ");

            if ((i+1) % 16 == 0) 
            {
                printf("|  %s \n", ascii);
            } 

            else if (i+1 == size) 
            {
                ascii[(i+1) % 16] = '\0';

                if ((i+1) % 16 <= 8) 
                {
                    //   printf(" ");
                }

                for (j = (i+1) % 16; j < 16; ++j) 
                {
                    printf("   ");
                }

                printf("|  %s \n", ascii);
            }
        }
    }
}



void poke ( uint32_t address, uint8_t data )
{
    write8 ( address, data );
}



/* configure ATARI MMU for amount of system RAM */
	//write8( 0xff8001, 0b00000100 ); // memory config 512k bank 0
    /*
    #define ATARI_MMU_128K  0b00000000 // bank 0
    #define ATARI_MMU_512K  0b00000100 // bank 0
    #define ATARI_MMU_2M    0b00001000 // bank 0
*/
void setMemory ( uint32_t size )
{
    uint8_t banks;

    switch (size)
    {
        case 512:
            banks = 0b00000100;
        break;

        case 1024:
            banks = 0b00000101;
        break;

        case 2048:
            banks = 0b00001000;
        break;

        case 4096:
            banks = 0b00001010;
        break;

        default:
            banks = 0b00000100;
        break;
    }

    write8 ( ((uint32_t)0xff8001), banks ); 
}


int memTest ( int direction, int type, uint32_t startAdd, uint32_t length, uint8_t *garbagePtr )
{
    uint8_t  d8, rd8;
    uint16_t d16, rd16;
    uint32_t d32, rd32;
    uint32_t radd;
    uint32_t add;
    long int nanoStart;
    long int nanoEnd; 

    char dirStr  [6];
    char typeStr [20];
    char testStr [80];

    struct timespec tmsStart, tmsEnd;
    static int testNumber;
    static int currentPass      = 0;
    int errors                  = 0;
    int thisTestErrors          = 0;
    int passErrors              = 0;


    if ( currentPass != cur_loop )
    {
        currentPass = cur_loop;
        testNumber = 1;
    }

    printf ( "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n" );

    switch (direction) 
    {
        case MEM_READ:

            sprintf ( dirStr, "READ" );

                switch (type)
                {
                    case TEST_8:

                        sprintf ( typeStr, "8:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[BYTE] Reading RAM...\n", testStr );

                        add = startAdd;

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0 ; add < length; n++, add++ ) 
                        {
                            if ( n == 0 )
                                printf ( "%-20sRunning ", testStr );

                            d8 = read8 (add);
                            
                            if ( d8 != garbagePtr [n] ) 
                            {
                                if ( thisTestErrors < 10 )
                                {
                                    if (thisTestErrors == 0)
                                        printf ( "\n" );

                                    printf ( "%-20s%sData mismatch at $%.6X: %.02X should be %.02X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        d8, 
                                        garbagePtr [n],
                                        NORMAL );
                                }

                                thisTestErrors++;
                            }

                            if (thisTestErrors && errorStop)
                            {
                                printf ( "%-20sStopped on error\n", testStr );
                                break;
                            }

                            if ( !thisTestErrors )
                            {
                                /* sanity feedback - one dot per 64 KB */
                                if ( n % (length / 32)  == 0 ) /* print 32 dots regardless of test length */
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        errors += thisTestErrors;

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_16:

                        sprintf ( typeStr, "16:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[WORD] Reading RAM aligned...\n", testStr );

                        add = startAdd;

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0; add < length - 2; n += 2, add += 2 ) 
                        {
                            if ( n == 0 )
                                printf ( "%-20sRunning ", testStr );

                            d16 = be16toh ( read16 (add) );
                            
                            if ( d16 != *( (uint16_t *) &garbagePtr [n] ) )
                            {
                                if ( thisTestErrors < 10 )
                                {
                                    if (thisTestErrors == 0)
                                        printf ( "\n" );

                                    printf ( "%-20s%sData mismatch at $%.6X: %.04X should be %.04X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        d16, 
                                        (uint16_t) garbagePtr [n + 1] << 8 | garbagePtr [n],
                                        NORMAL );
                                }

                                thisTestErrors++;
                            }

                            //errors += thisTestErrors;

                            if (thisTestErrors && errorStop)
                            {
                                printf ( "%-20sStopped on error\n", testStr );
                                break;
                            }

                            if ( !thisTestErrors )
                            {
                                if ( n % (length / 32)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        errors += thisTestErrors;

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_16_ODD:

                        sprintf ( typeStr, "16_ODD:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[WORD] Reading RAM unaligned...\n", testStr );

                        add = startAdd + 1;

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 1; add < length - 2; n += 2, add += 2 ) 
                        {
                            if ( n == 1 )
                                printf ( "%-20sRunning ", testStr );

                            d16 = be16toh ( (read8 (add) << 8) | read8 ( add + 1 ) );
                            
                            if ( d16 != *( (uint16_t *) &garbagePtr [n] ) )
                            {
                                if ( thisTestErrors < 10 )
                                {
                                    if (thisTestErrors == 0)
                                        printf ( "\n" );

                                    printf ( "%-20s%sData mismatch at $%.6X: %.04X should be %.04X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        d16, 
                                        *( (uint16_t *) &garbagePtr [n] ),
                                        NORMAL );
                                }

                                thisTestErrors++;
                            }

                           // errors += thisTestErrors;

                            if (thisTestErrors && errorStop)
                            {
                                printf ( "%-20sStopped on error\n", testStr );
                                break;
                            }

                            if ( !thisTestErrors )
                            {
                                if ( (n - 1) % (length / 32)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        errors += thisTestErrors;

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_32:

                        sprintf ( typeStr, "32:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[LONG] Reading RAM aligned...\n", testStr );

                        add = startAdd;

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0; add < length - 4; n += 4, add += 4 ) 
                        {
                            if ( n == 0 )
                                printf ( "%-20sRunning ", testStr );

                            d32 = be32toh ( read32 (add) );
                            
                            if ( d32 != *( (uint32_t *) &garbagePtr [n] ) )
                            {
                                if ( thisTestErrors < 10 )
                                {
                                    if (thisTestErrors == 0)
                                        printf ( "\n" );

                                    printf ( "%-20s%sData mismatch at $%.6X: %.08X should be %.08X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        d32, 
                                        *( (uint32_t *) &garbagePtr [n] ),
                                        NORMAL );
                                }

                                thisTestErrors++;
                            }

                            //errors += thisTestErrors;

                            if (thisTestErrors && errorStop)
                            {
                                printf ( "%-20sStopped on error\n", testStr );
                                break;
                            }

                            if ( !thisTestErrors )
                            {
                                if ( n % (length / 32)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        errors += thisTestErrors;

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_32_ODD:

                        sprintf ( typeStr, "32_ODD:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[LONG] Reading RAM unaligned...\n", testStr );

                        add = startAdd + 1;

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 1; add < length - 4; n += 4, add += 4 ) 
                        {
                            if ( n == 1 )
                                printf ( "%-20sRunning ", testStr );

                            d32 = read8 (add);
                            d32 |= (be16toh ( read16 ( add + 1 ) ) << 8);
                            d32 |= (read8 ( add + 3 ) << 24 );
                            
                            if ( d32 != *( (uint32_t *) &garbagePtr [n] ) )
                            {
                                if ( thisTestErrors < 10 )
                                {
                                    if (thisTestErrors == 0)
                                        printf ( "\n" );

                                    printf ( "%-20s%sData mismatch at $%.6X: %.08X should be %.08X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        d32, 
                                        *( (uint32_t *) &garbagePtr [n] ),
                                        NORMAL );
                                }

                                thisTestErrors++;
                            }

                            //errors += thisTestErrors;

                            if (thisTestErrors && errorStop)
                            {
                                printf ( "%-20sStopped on error\n", testStr );
                                break;
                            }

                            if ( !thisTestErrors )
                            {
                                if ( (n - 1) % (length / 32)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        errors += thisTestErrors;

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    /* random data / random addresses */
                    case TEST_8_RANDOM:

                        srand (length);

                        sprintf ( typeStr, "8_RANDOM_RW:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[BYTE] Writing random data to random addresses...\n", testStr );

                        add = startAdd;

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0; add < length; n++, add++ ) 
                        {
                            if ( n == 0 )
                                printf ( "%-20sRunning ", testStr );

                            rd8  = (uint8_t)  ( rand () % 0xFF );

                            for ( int z = 10; z; z-- ) /* ten retries should be enough especially for small mem size */
                            {
                                radd = (uint32_t) ( rand () % length );

                                if ( radd < startAdd )
                                    continue;

                                break;
                            }

                            write8 ( radd, rd8 );
                            d8 = read8 (radd);
                            
                            if ( d8 != rd8 ) 
                            {
                                if ( thisTestErrors < 10 )
                                {
                                    if (thisTestErrors == 0)
                                        printf ( "\n" );

                                    //printf ( "\n%sData mismatch at $%.6X: %.02X should be %.02X\n", testStr, radd, d8, rd8 );
                                    printf ( "%-20s%sData mismatch at $%.6X: %.02X should be %.02X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        radd, 
                                        d8, 
                                        rd8,
                                        NORMAL );
                                }

                                thisTestErrors++;
                            }

                            //errors += thisTestErrors;

                            if (thisTestErrors && errorStop)
                            {
                                printf ( "%-20sStopped on error\n", testStr );
                                break;
                            }

                            if ( !thisTestErrors )
                            {
                                /* sanity feedback - one dot per 64 KB */
                                if ( n % (length / 32)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        errors += thisTestErrors;

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_16_RANDOM:

                        srand (length);

                        sprintf ( typeStr, "16_RANDOM_RW:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[WORD] Writing random data to random addresses aligned...\n", testStr );

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0, add = startAdd ; add < length - 2; n += 2, add += 2 ) 
                        {
                            if ( n == 0 )
                                printf ( "%-20sRunning ", testStr );

                            rd16  = (uint16_t)  ( rand () % 0xffff );

                            for ( int z = 10; z; z-- ) /* ten retries should be enough especially for small mem size */
                            {
                                radd = (uint32_t) ( rand () % length );

                                if ( radd < startAdd )
                                    continue;

                                break;
                            }

                            write16 ( radd, rd16 );
                            d16 = read16 (radd);
                            //d16 = be16toh ( read16 (add) );
                            
                            if ( d16 != rd16 ) 
                            {
                                if ( thisTestErrors < 10 )
                                {
                                    if (thisTestErrors == 0)
                                        printf ( "\n" );

                                    //printf ( "\n%sData mismatch at $%.6X: %.04X should be %.04X\n", testStr, radd, d16, rd16 );
                                    printf ( "%-20s%sData mismatch at $%.6X: %.04X should be %.04X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        radd, 
                                        d16, 
                                        rd16,
                                        NORMAL );
                                }

                                thisTestErrors++;
                            }

                            //errors += thisTestErrors;

                            if (thisTestErrors && errorStop)
                            {
                                printf ( "%-20sStopped on error\n", testStr );
                                break;
                            }

                            if ( !thisTestErrors )
                            {
                                /* sanity feedback - one dot per 64 KB */
                                if ( n % (length / 32)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        errors += thisTestErrors;

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_32_RANDOM:

                        srand (length);

                        sprintf ( typeStr, "32_RANDOM_RW:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[LONG] Writing random data to random addresses aligned...\n", testStr );

                        add = startAdd;

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0; add < length - 4; n += 4, add += 4 ) 
                        {
                            if ( n == 0 )
                                printf ( "%-20sRunning ", testStr );

                            rd32  = (uint32_t)  ( rand () % 0xffffffff );

                            for ( int z = 10; z; z-- ) /* ten retries should be enough especially for small mem size */
                            {
                                radd = (uint32_t) ( rand () % length );

                                if ( radd < startAdd )
                                    continue;

                                break;
                            }

                            write32 ( radd, rd32 );
                            d32 = read32  ( radd );
                            
                            if ( d32 != rd32 ) 
                            {
                                if ( thisTestErrors < 10 )
                                {
                                    if (thisTestErrors == 0)
                                        printf ( "\n" );

                                    //printf ( "\n%sData mismatch at $%.6X: %.08X should be %.08X\n", testStr, radd, d32, rd32 );
                                    printf ( "%-20s%sData mismatch at $%.6X: %.08X should be %.08X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        radd, 
                                        d32, 
                                        rd32,
                                        NORMAL );
                                }

                                thisTestErrors++;
                            }

                            //errors += thisTestErrors;

                            if (thisTestErrors && errorStop)
                            {
                                printf ( "%-20sStopped on error\n", testStr );
                                break;
                            }

                            if ( !thisTestErrors )
                            {
                                /* sanity feedback - one dot per 64 KB */
                                if ( n % (length / 32)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        errors += thisTestErrors;

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;
                }
            

        break;

        case MEM_WRITE:

            sprintf ( dirStr, "WRITE" );

            switch (type)
            {
                case TEST_8:

                    sprintf ( typeStr, "8:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[BYTE] Writing to RAM... \n", testStr );

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    add = startAdd;

                    for ( uint32_t n = 0; add < length; n++, add++ ) 
                    {
                        if ( n == 0 )
                            printf ( "%-20sRunning ", testStr );

                        d8 = garbagePtr [n];

                        write8 ( add, d8 );
                        rd8 = read8  ( add );
                        
                        if ( d8 != rd8 ) 
                        {
                            if ( thisTestErrors < 10 )
                            {
                                if (thisTestErrors == 0)
                                    printf ( "\n" );

                                //printf ( "\n%sData mismatch at $%.6X: %.02X should be %.02X\n", testStr, add, rd8, d8 );
                                printf ( "%-20s%sData mismatch at $%.6X: %.02X should be %.02X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        rd8, 
                                        d8,
                                        NORMAL );
                            }

                            thisTestErrors++;
                        }

                        //errors += thisTestErrors;

                        if (thisTestErrors && errorStop)
                        {
                            printf ( "%-20sStopped on error\n", testStr );
                            break;
                        }

                        if ( !thisTestErrors )
                        {
                            if ( n % (length / 32)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }
                    }

                    errors += thisTestErrors;

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;

                case TEST_16:

                    sprintf ( typeStr, "16:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[WORD] Writing to RAM aligned... \n", testStr );

                    add = startAdd;

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    for ( uint32_t n = 0; add < length - 2; n += 2, add += 2) 
                    {
                        if ( n == 0 )
                            printf ( "%-20sRunning ", testStr );

                        d16 = *( (uint16_t *) &garbagePtr [n] );

                        write16 ( add, d16 );
                        rd16 = read16  ( add );
                        
                        if ( d16 != rd16 ) 
                        {
                            if ( thisTestErrors < 10 )
                            {
                                if (thisTestErrors == 0)
                                    printf ( "\n" );

                                //printf ( "\n%sData mismatch at $%.6X: %.04X should be %.04X\n", testStr, add, rd16, d16 );
                                printf ( "%-20s%sData mismatch at $%.6X: %.04X should be %.04X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        rd16, 
                                        d16,
                                        NORMAL );
                            }

                            thisTestErrors++;
                        }

                        //errors += thisTestErrors;

                        if (thisTestErrors && errorStop)
                        {
                            printf ( "%-20sStopped on error\n", testStr );
                            break;
                        }

                        if ( !thisTestErrors )
                        {
                            if ( n % (length / 32)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }
                    }

                    errors += thisTestErrors;

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;

                case TEST_16_ODD:

                    sprintf ( typeStr, "16_ODD:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[WORD] Writing to RAM unaligned... \n", testStr );

                    add = startAdd + 1;

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    for ( uint32_t n = 1; add < length - 2; n += 2, add += 2) 
                    {
                        if ( n == 1 )
                            printf ( "%-20sRunning ", testStr );

                        d16 = *( (uint16_t *) &garbagePtr [n] );

                        write8 ( add, (d16 & 0x00FF) );
                        write8 ( add + 1, (d16 >> 8) );                      
                        rd16 = be16toh ( (read8 (add) << 8) | read8 ( add + 1 ) );

                        if ( d16 != rd16 ) 
                        {
                            if ( thisTestErrors < 10 )
                            {
                                if (thisTestErrors == 0)
                                    printf ( "\n" );

                                //printf ( "\n%sData mismatch at $%.6X: %.04X should be %.04X\n", testStr, add, rd16, d16 );
                                printf ( "%-20s%sData mismatch at $%.6X: %.04X should be %.04X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        rd16, 
                                        d16,
                                        NORMAL );
                            }

                            thisTestErrors++;
                        }

                        //errors += thisTestErrors;

                        if (thisTestErrors && errorStop)
                        {
                            printf ( "%-20sStopped on error\n", testStr );
                            break;
                        }

                        if ( !thisTestErrors )
                        {
                            if ( (n - 1) % (length / 32)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }
                    }

                    errors += thisTestErrors;

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;

                case TEST_32:

                    sprintf ( typeStr, "32:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[LONG] Writing to RAM aligned... \n", testStr );

                    add = startAdd;

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    for ( uint32_t n = 0; add < length - 4; n += 4, add += 4) 
                    {
                        if ( n == 0 )
                                printf ( "%-20sRunning ", testStr );

                        d32 = *( (uint32_t *) &garbagePtr [n] );

                        write32 ( add, d32 );
                        rd32 = read32 ( add );

                        if ( d32 != rd32 ) 
                        {
                            if ( thisTestErrors < 10 )
                            {
                                if (thisTestErrors == 0)
                                    printf ( "\n" );

                                //printf ( "\n%sData mismatch at $%.6X: %.08X should be %.08X\n", testStr, add, rd32, d32 );
                                printf ( "%-20s%sData mismatch at $%.6X: %.08X should be %.08X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        rd32, 
                                        d32,
                                        NORMAL );
                            }

                            thisTestErrors++;
                        }

                        //errors += thisTestErrors;

                        if (thisTestErrors && errorStop)
                        {
                            printf ( "%-20sStopped on error\n", testStr );
                            break;
                        }

                        if ( !thisTestErrors )
                        {
                            if ( n % (length / 32)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }
                    }

                    errors += thisTestErrors;

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;

                case TEST_32_ODD:

                    sprintf ( typeStr, "32_ODD:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[LONG] Writing to RAM unaligned... \n", testStr );

                    add = startAdd + 1;

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    for ( uint32_t n = 1; add < length - 4; n += 4, add += 4) 
                    {
                        if ( n == 1 )
                            printf ( "%-20sRunning ", testStr );

                        d32 = *( (uint32_t *) &garbagePtr [n] );

                        write8  ( add, (d32 & 0x0000FF) );
                        write16 ( add + 1, htobe16 ( ( (d32 & 0x00FFFF00) >> 8) ) );
                        write8  ( add + 3, (d32 & 0xFF000000) >> 24);

                        rd32  = read8 (add);
                        rd32 |= (be16toh ( read16 ( add + 1 ) ) << 8);
                        rd32 |= (read8 ( add + 3 ) << 24 );

                        if ( d32 != rd32 ) 
                        {
                            if ( thisTestErrors < 10 )
                            {
                                if (thisTestErrors == 0)
                                    printf ( "\n" );

                                //printf ( "\n%sData mismatch at $%.6X: %.08X should be %.08X\n", testStr, add, rd32, d32 );
                                printf ( "%-20s%sData mismatch at $%.6X: %.08X should be %.08X%s\n", 
                                        testStr,
                                        REVERSE_VIDEO, 
                                        add, 
                                        rd32, 
                                        d32,
                                        NORMAL );
                            }

                            thisTestErrors++;
                        }

                        //errors += thisTestErrors;

                        if (thisTestErrors && errorStop)
                        {
                            printf ( "%-20sStopped on error\n", testStr );
                            break;
                        }

                        if ( !thisTestErrors )
                        {
                            if ( (n - 1) % (length / 32)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }
                    }

                    errors += thisTestErrors;

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;
            }
                
        break;
    }

    if ( errors && errorStop )
    {
        return 0;
    }

    nanoStart = (tmsStart.tv_sec * 1000) + (tmsStart.tv_nsec / 1000000);
    nanoEnd   = (tmsEnd.tv_sec * 1000) + (tmsEnd.tv_nsec / 1000000);

    uint32_t calcLength = (length - startAdd);

    /* recalculate data transfer size as the MEM_WRITES have a read and write component */
    if ( direction == MEM_WRITE ) //&& (type == TEST_8 || type == TEST_8_ODD || type == TEST_8_RANDOM) )
    {
        calcLength *= 2;
    }

    else if ( direction == MEM_READ && (type == TEST_8_RANDOM || type == TEST_16_RANDOM || type == TEST_32_RANDOM) )
    {
        calcLength *= 2;
    }

    printf ( "%-20sTest %d Completed with %d %s in %d ms (%.2f MB/s)\n", 
        testStr, 
        testNumber,
        thisTestErrors,
        thisTestErrors == 1 ? "error" : "errors",
        (nanoEnd - nanoStart), 
       // (( (float)calcLength / (float)(nanoEnd - nanoStart)) * 1000.0) / 1024 );     /* KB/s */
         ( 1.0 / ( (float)(nanoEnd - nanoStart) ) * calcLength ) / 1024 );     /* MB/s */

        
    
    //if ( thisTestErrors )
    //    printf ( "%-20s%sTest errors = %08d%s\n\n", 
    //        testStr,
    //        thisTestErrors ? REVERSE_VIDEO : "",
    //        thisTestErrors,
    //        thisTestErrors ? NORMAL : "" );

    printf ( "++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++\n\n" );

    testNumber++;
    totalErrors += errors;

    return 1;
}


/* no checks performed - raw performance reported */
void clearmem ( uint32_t length, uint32_t *duration, uint16_t pattern )
{
    struct timespec tmsStart, tmsEnd;


    clock_gettime ( CLOCK_REALTIME, &tmsStart );
    
    for ( uint32_t n = 8; n < length; n += 2 ) {

        write16 ( n, pattern );

        if ( n % (length / 64)  == 0 ) 
        {
            printf ( "." );
            fflush ( stdout );
        }
    }

    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

    long int nanoStart = (tmsStart.tv_sec * 1000) + (tmsStart.tv_nsec / 1000000);
    long int nanoEnd = (tmsEnd.tv_sec * 1000) + (tmsEnd.tv_nsec / 1000000);

    *duration = (nanoEnd - nanoStart);
}



void dump ( uint32_t ROMsize, uint32_t ROMaddress )
{
    uint8_t  in;
    FILE *out = fopen ("tos.rom", "wb+" );

    if ( out == NULL ) 
    {
        printf ("Failed to open tos.rom for writing.\nTOS has not been dumped.\n");

        return;
    }

    ROMsize = ROMsize * SIZE_KILO;

    for ( int i = 0; i < ROMsize; i++ )
    {
        in = read8 ( (ROMaddress + i) ) ;

        fputc ( in, out );
    }

    fclose (out);
}



/* command line parser */
int parser ( int argc, char **argv )
{
    int valid = 0;
    char commandLine [80];
    char *ptr;
    int syntax = 0;
    char arguments [80];
    char *tptr, *aptr;
    char *cmdptr, *argptr;
    char cmd [80];
    char arg [80];
    char *savePtr, *cmdSave;


    for ( int a = 1; a < argc; a++ )
    {
        strncpy ( cmd, argv [a], 80 );
        //printf ( "argv [%d] = %s\n", a, argv [a] );
        
        cmdptr = strtok_r ( cmd, "--", &cmdSave );
        //printf ("cmdptr = %s\n", cmdptr );
      
        if ( strcmp ( cmdptr, "memory" ) == 0 )
        {        
            valid = 1;

            for ( int z = 3; z && valid && a < argc - 1; z-- )
            {
                strncpy ( arg, argv [++a], 80 );
                argptr = strtok_r ( arg, " ", &savePtr );
                aptr = strtok ( argptr, "=" );

                if ( strcmp ( aptr, "tests" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );

                    if ( *tptr == 'r' || *(tptr + 1) == 'r' || *(tptr + 2) == 'r'  )
                        doReads = 1;

                    if ( *tptr == 'w' || *(tptr + 1) == 'w' || *(tptr + 2) == 'w'  )
                        doWrites = 1;

                    if ( *tptr == 'x' || *(tptr + 1) == 'x' || *(tptr + 2) == 'x'  )
                        doRandoms = 1;
                }

                else if ( strcmp ( aptr, "size" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    testSize = atoi (tptr);

                    if ( testSize < 512 )
                        testSize = 512;

                    if ( testSize > 4096 )
                        testSize = 4096;
                }

                else if ( strcmp ( aptr, "loop" ) == 0 )
                {   
                    tptr = strtok ( NULL, "" );

                    if ( strcmp ( tptr, "yes" ) == 0 )
                        loopTests = 1;
                }

                else if ( strcmp ( aptr, "stop" ) == 0 )
                {   
                    tptr = strtok ( NULL, "" );

                    if ( strcmp ( tptr, "yes" ) == 0 )
                        errorStop = 1;
                }

                else
                    valid = 0;

                argptr = strtok ( savePtr, " " );
            }

            if ( valid )
                cmdMem = 1;
        }

        if ( strcmp ( cmdptr, "clearmem" ) == 0 )
        {
            valid = 1; 

            for ( int z = 3; z && valid && a < argc - 1; z-- )
            {
                strncpy ( arg, argv [++a], 80 );
                argptr = strtok_r ( arg, " ", &savePtr );
                aptr = strtok ( argptr, "=" );
        
                if ( strcmp ( aptr, "size" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    testSize = atoi ( tptr );

                    if ( testSize < 512 )
                        testSize = 512;

                    if ( testSize > 4096 )
                        testSize = 4096;
                }

                else if ( strcmp ( aptr, "pattern" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    sscanf ( tptr, "%x", &clrPattern );
                } 

                else
                    valid = 0;

                argptr = strtok ( savePtr, " " );
            }

            if ( valid )
            {
                cmdClear = 1; 
            }   
        }

        if ( strcmp ( cmdptr, "peek" ) == 0 )
        {
            valid = 1;

            for ( int z = 2; z && valid && a < argc - 1; z-- )
            {
                strncpy ( arg, argv [++a], 80 );
                argptr = strtok_r ( arg, " ", &savePtr );
                aptr = strtok ( argptr, "=" );
      
                if ( strcmp ( aptr, "address" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    sscanf ( tptr, "%x", &padd );
                }

                else if ( strcmp ( aptr, "loop" ) == 0 )
                {   
                    tptr = strtok ( NULL, "" );

                    if ( strcmp ( tptr, "yes" ) == 0 )
                        loopTests = 1;
                }

                else
                    valid = 0;

                argptr = strtok ( savePtr, " " );
            }

            if ( valid )
            {
                cmdPeek = 1;
            }
        }

        if ( strcmp ( cmdptr, "poke" ) == 0 )
        {
            valid = 1; 

            for ( int z = 3; z && valid && a < argc - 1; z-- )
            {
                strncpy ( arg, argv [++a], 80 );
                argptr = strtok_r ( arg, " ", &savePtr );
                aptr = strtok ( argptr, "=" );
        
                if ( strcmp ( aptr, "address" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    sscanf ( tptr, "%x", &padd );
                }

                else if ( strcmp ( aptr, "data" ) == 0 )
                {   
                    tptr = strtok ( NULL, "" );
                    sscanf ( tptr, "%x", &pdata );
                }

                else
                    valid = 0;

                argptr = strtok ( savePtr, " " );
            }

            if ( valid )
            {
                cmdPoke = 1;
            }
        }

        //  syntax --dumprom size=256 address=0xe00000 
        if ( strcmp ( cmdptr, "dumprom" ) == 0 )
        {
            valid = 1; 

            for ( int z = 3; z && valid && a < argc - 1; z-- )
            {
                strncpy ( arg, argv [++a], 80 );
                argptr = strtok_r ( arg, " ", &savePtr );
                aptr = strtok ( argptr, "=" );
        
                if ( strcmp ( aptr, "address" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    sscanf ( tptr, "%x", &ROMaddress );
                }

                else if ( strcmp ( aptr, "size" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    ROMsize = atoi (tptr);

                    if ( ROMsize > 256 || ROMsize < 192 )
                        ROMsize = 192;
                }

                else
                    valid = 0;

                argptr = strtok ( savePtr, " " );
            }

            if ( valid )
                cmdDump = 1;
        }

        if ( strcmp ( cmdptr, "init" ) == 0 )
        {
            valid = 1; 

            for ( int z = 2; z && valid && a < argc - 1; z-- )
            {
                strncpy ( arg, argv [++a], 80 );
                argptr = strtok_r ( arg, " ", &savePtr );
                aptr = strtok ( argptr, "=" );

                if ( strcmp ( aptr, "size" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    memSize = atoi ( tptr );

                    if ( memSize < 512 )
                        memSize = 512;

                    if ( memSize > 4096 )
                        memSize = 4096;
                }

                else
                    valid = 0;

                argptr = strtok ( savePtr, " " );
            }

            if ( valid )
                cmdInit = 1;
        }

        if ( strcmp ( cmdptr, "memspeed" ) == 0 )
        {
            valid = 1;

            for ( int z = 3; z && valid && a < argc - 1; z-- )
            {
                strncpy ( arg, argv [++a], 80 );
                argptr = strtok_r ( arg, " ", &savePtr );
                aptr = strtok ( argptr, "=" );
        
                if ( strcmp ( aptr, "size" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
                    testSize = atoi ( tptr );

                    if ( testSize < 512 )
                        testSize = 512;

                    if ( testSize > 4096 )
                        testSize = 4096;
                } 

                else
                    valid = 0;

                argptr = strtok ( savePtr, " " );
            }

            if ( valid )
                cmdMemSpeed = 1;
        }
    
        if ( strcmp ( cmdptr, "clock" ) == 0 )
        {
            //valid = 1;
            char *p;
            //printf ( "argv = %s\n", argv[a+1] );
            targetF = strtol ( argv [a+1], &p, 10 );
            //printf ( "targetF = %d\n", targetF );
        }

        if ( strcmp ( cmdptr, "hardware" ) == 0 )
        {
            cmdHWTEST = 1;
            valid = 1;
        }
    }

    return valid | syntax;
}



#define SIZE_KILO 1024
#define SIZE_MEGA (1024 * 1024)
#define SIZE_GIGA (1024 * 1024 * 1024)

//uint8_t garbege_datas[4 * SIZE_MEGA];

void hwTest ( void )
{
    uint16_t tmp;
    volatile uint32_t bit;
    uint32_t test_size = 512 * SIZE_KILO, cur_loop = 0;
    uint8_t loop_tests = 0, total_errors = 0;

    test_size = 512 * SIZE_KILO;
            
    garbege_datas = malloc ( test_size );

    if ( !garbege_datas )
    {
        printf ( "Failed to allocate memory for garbege datas\n" );

        return;
    }

    printf ( "\nThe PiStorm must be installed, powered and flashed with the latest firmware\n" );
    printf ( "The following tests are meant as a simple confidence check and require a fully functioning Atari\n\n" );
    printf ( "Testing PiStorm Hardware\n\n" );

test_loop:
    /* Check Address lines by writing two data bit patterns and reading them back */
    /* The upper address bits (A20, A21) can pnly be cjecked if the Atari system has memory expansion */
    /* Address lines A22, A23 */
    printf ( "Address line test\n" );
    printf ( "NOTE A19, A20, A21 will only pass if RAM expansion is installed - A22, A23 can not be tested\n" );
    for ( int n = 0; n < 24; n++ )
    {
        bit = 1 << n;
        printf ( "A%02d $%.6X... ", n, bit );

        write8 ( 0x10000 + bit, n );    
        tmp = read8 ( 0x10000 + bit );

        if ( tmp != n )
        {
            if ( tmp == 0xFF )
            {
                if ( n > 18 && n < 24 )
                    printf ( "No memory detected\n" );

                else
                {
                    printf ( "Faulty\n" );
                    errors++;
                }
            }

            else
            {
                printf ( "Wrote 0x%X, Read 0x%X\n", n, tmp );
                errors++;
            }
        }

        else 
        {
            printf ( "\n" );
/*
            write8 ( 0x10000 + bit, 0xAA );    
            tmp = read8 ( 0x10000 + bit );

            if ( tmp != 0xAA )
            {
                printf ( "Wrote 0x%X, Read 0x%X\n", 0xAA, tmp );
                errors++;
            }
*/
        }

    }
/*
    if ( errors )
    {
        printf ( "Address line errors have been detected. Further testing can not continue\n" );

        return;
    }
*/
    printf ( "\nAddress line test completed\n" );

    /* ---------------------------------------------------------------------- */

    uint16_t c;

    printf ( "\nData bus test (read/write)\n" );
	//printf ( "NOTE works on non-A variant flip-flops (373 or 374's not 373A or 374A\n" );
	

    for ( int n = 0; n < 16; n++ )
    {
        tmp = 1 << n;

        printf ( "D%.02d $%.4X... ", n, tmp );

        write16 ( 0x10000 + (n * 2), tmp );
        
        c = read16 ( 0x10000 + (n * 2) );
      
        if ( c != tmp ) 
        {
            printf ( "Wrote 0x%X, Read 0x%X\n",  tmp, c );
            errors++;
        }

        else 
        {
            printf ( "\n" );
        }
    }

    printf ( "\nData bus test completed\n" );
    
    /* ---------------------------------------------------------------------- */

    printf ( "\nHardware total errors: %d\n", errors );

    total_errors += errors;
    errors = 0;
    sleep (1);

    if ( loop_tests ) 
    {
        printf ( "Loop %d done. Begin loop %d\n", cur_loop + 1, cur_loop + 2 );
        printf ( "Current total errors: %d\n", total_errors );

        goto test_loop;
    }

    return;
}