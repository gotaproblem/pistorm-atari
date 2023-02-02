// SPDX-License-Identifier: MIT

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
void memTest ( int direction, int type, uint32_t startAdd, uint32_t length, uint8_t *garbagePtr );
void clearmem ( uint32_t length, uint32_t *duration );
void setMemory ( uint32_t size );
void peek ( uint32_t start );
void dump ( uint32_t ROMsize, uint32_t ROMaddress );


int doReads;
int doWrites;
int doRandoms;
uint32_t testSize;
uint32_t memSize;
int loopTests;
uint32_t padd;
int cmdPeek = 0;
int cmdMem = 0;
int cmdDump = 0;
int cmdClear = 0;
uint32_t ROMsize = 192;
uint32_t ROMaddress = 0x00e00000;


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

    /* assuming this is for AMIGA ??? nothing in ATARI memory map */
    //write8(0xbfe201, 0x0101);       //CIA OVL
	//write8(0xbfe001, 0x0000);       //CIA OVL LOW
}
/*
unsigned int dump_read_8(unsigned int address) {
    uint32_t bwait = 10000;

    *(gpio + 0) = GPFSEL0_OUTPUT;
    *(gpio + 1) = GPFSEL1_OUTPUT;
    *(gpio + 2) = GPFSEL2_OUTPUT;

    *(gpio + 7) = ((address & 0xffff) << 8) | (REG_ADDR_LO << PIN_A0);
    *(gpio + 7) = 1 << PIN_WR;
    *(gpio + 10) = 1 << PIN_WR;
    *(gpio + 10) = 0xffffec;

    *(gpio + 7) = ((0x0300 | (address >> 16)) << 8) | (REG_ADDR_HI << PIN_A0);
    *(gpio + 7) = 1 << PIN_WR;
    *(gpio + 10) = 1 << PIN_WR;
    *(gpio + 10) = 0xffffec;

    *(gpio + 0) = GPFSEL0_INPUT;
    *(gpio + 1) = GPFSEL1_INPUT;
    *(gpio + 2) = GPFSEL2_INPUT;

    *(gpio + 7) = (REG_DATA << PIN_A0);
    *(gpio + 7) = 1 << PIN_RD;


    //while (bwait && (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS))) {
    while ( (*(gpio + 13) & (1 << PIN_TXN_IN_PROGRESS))) {
       // bwait--;
    }

    unsigned int value = *(gpio + 13);

    *(gpio + 10) = 0xffffec;

    value = (value >> 8) & 0xffff;

    //if ( !bwait ) {
    //    ps_reinit();
    //}

    if ((address & 1) == 0)
        return (value >> 8) & 0xff;  // EVEN, A0=0,UDS
    else
        return value & 0xff;  // ODD , A0=1,LDS
}
*/


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
    


    cur_loop = 1;

    if (check_emulator()) {
        printf("PiStorm emulator running, please stop this before running ataritest\n");
        return 1;
    }

    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &f2);
    srand((unsigned int)f2.tv_nsec);

    signal(SIGINT, sigint_handler);

    ps_setup_protocol();
    ps_reset_state_machine();
    ps_pulse_reset();

    usleep(1500);

	fc = 0b101;
    write8( 0xff8001, 0b00001010 ); // memory config 512k bank 0
    
    doReads = 0;
    doWrites = 0;
    doRandoms = 0;
    memSize = 0;
    testSize = 512;
    loopTests = 0;

   
    if ( parser ( argc, argv ) )
    {
        printf ( "\nATARITEST\n" );

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

        /* must be 512 or 1024 or 2048 or 4096 */
        //setMemory ( memSize );

        printf ( "ATARI MMU configured to use %d KB\n", memSize );

        if ( cmdClear )
        {
            uint32_t duration;

            printf ( "\nClearing ATARI ST RAM - %d KB\n", testSize );
            fflush (stdout);

            clearmem ( testSize * SIZE_KILO, &duration );
            
            printf ( "\nATARI ST RAM cleared in %d ms @ %.2f KB/s\n\n", 
                duration, ( (float)((testSize * 1024) - OFFSET) / (float)duration * 1000.0) / 1024 );
        }

        if ( cmdPeek )
        {
            peek ( padd );
        }

        if ( cmdDump )
        {
            printf ( "Dumping onboard ATARI ROM from 0x%X to file tos.rom\n", ROMaddress );

            dump ( ROMsize, ROMaddress );

            printf ( "ATARI ROM dumped - %d KB\n", ROMsize );
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
                printf ( "Failed to allocate memory for garbege datas.\n" );

                return 1;
            }

            printf ( "Testing %d KB of memory - Starting address $%.6X\n", test_size / SIZE_KILO, OFFSET );

            if ( loop_tests )
                printf ( "Test looping enabled\n" );

            if (doWrites)
            {
                printf ( "Priming test data.\n");
                
                for ( uint32_t i = 0, add = OFFSET ; add < test_size; i++, add++ ) 
                {
                    garbege_datas [i] = add % 2 ? (add - 1 >> 8) & 0xff : add & 0xff;

                    write8 ( add, garbege_datas [i] );
                }
            }

test_loop:

            if (doReads)
            {
                printf ( "\n%sTesting Reads...%s\n\n", REVERSE_VIDEO, NORMAL );

                memTest ( MEM_READ,  TEST_8,         OFFSET, test_size, garbege_datas );
                memTest ( MEM_READ,  TEST_16,        OFFSET, test_size, garbege_datas );
                memTest ( MEM_READ,  TEST_16_ODD,    OFFSET, test_size, garbege_datas );
                memTest ( MEM_READ,  TEST_32,        OFFSET, test_size, garbege_datas );
                memTest ( MEM_READ,  TEST_32_ODD,    OFFSET, test_size, garbege_datas );
            }

            if (doWrites)
            {
                printf ( "\n%sTesting Writes...%s\n\n", REVERSE_VIDEO, NORMAL );

                memTest ( MEM_WRITE, TEST_8,         OFFSET, test_size, garbege_datas );
                memTest ( MEM_WRITE, TEST_16,        OFFSET, test_size, garbege_datas );
                memTest ( MEM_WRITE, TEST_16_ODD,    OFFSET, test_size, garbege_datas );
                memTest ( MEM_WRITE, TEST_32,        OFFSET, test_size, garbege_datas );
                memTest ( MEM_WRITE, TEST_32_ODD,    OFFSET, test_size, garbege_datas );
            }

            if (doRandoms)
            {
                printf ( "\n%sTesting Random Reads / Writes...%s\n\n", REVERSE_VIDEO, NORMAL );

                memTest ( MEM_READ,  TEST_8_RANDOM,  OFFSET, test_size, garbege_datas );
                memTest ( MEM_READ,  TEST_16_RANDOM, OFFSET, test_size, garbege_datas );
                memTest ( MEM_READ,  TEST_32_RANDOM, OFFSET, test_size, garbege_datas );
            }

            if (loop_tests) 
            {
                printf ( "\nPass %d complete\nStarting pass %d\n", cur_loop, cur_loop + 1);

                sleep(1);

                printf ( "Priming test data\n" );

                for ( uint32_t i = 0, add = OFFSET ; add < test_size; i++, add++ ) 
                {
                    garbege_datas [i] = (uint8_t)(rand() % 0xFF );
                    write8 ( add, (uint32_t) garbege_datas [i] );
                }

                cur_loop++;

                goto test_loop;
            }
        }
    }

    else
    {
        printf ( "ATARITEST syntax error\n" );

        exit (0);
    }

    printf ( "\nATARITEST complete\n\n");

    return 0;
}


void m68k_set_irq(unsigned int level) {
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


/* configure ATARI MMU for amount of system RAM */
	//write8( 0xff8001, 0b00000100 ); // memory config 512k bank 0
    /*
    #define ATARI_MMU_128K  0b00000000 // bank 0
    #define ATARI_MMU_512K  0b00000100 // bank 0
    #define ATARI_MMU_2M    0b00001000 // bank 0
*/
void setMemory ( uint32_t size )
{
    uint8_t banks = 0b00001010; // configure MMU for 4MB - configure banks 0 AND 1

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

    write8 ( 0xff8001, banks ); 
}


void memTest ( int direction, int type, uint32_t startAdd, uint32_t length, uint8_t *garbagePtr )
{
    uint8_t  d8, rd8;
    uint16_t d16, rd16;
    uint32_t d32, rd32;
    uint32_t radd;
    long int nanoStart;
    long int nanoEnd; 

    char dirStr  [6];
    char typeStr [20];
    char testStr [80];

    struct timespec tmsStart, tmsEnd;

    static uint32_t totalErrors = 0;
    static int testNumber;
    static int currentPass      = 0;
    int errors                  = 0;


    if ( currentPass != cur_loop )
    {
        currentPass = cur_loop;
        testNumber = 1;
    }

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

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0, add = startAdd ; add < length; n++, add++ ) 
                        {
                            d8 = read8 (add);
                            
                            if ( d8 != garbagePtr [n] ) 
                            {
                                if ( errors < 10 )
                                {
                                    if (errors == 0)
                                        printf ( "\n" );

                                    printf ( "%sData mismatch at $%.6X: %.02X should be %.02X\n", testStr, add, d8, garbagePtr [n] );
                                }

                                errors++;
                            }

                            /* sanity feedback - one dot per 64 KB */
                            if ( n % (length / 64)  == 0 ) /* print 64 dots regardless of test length */
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_16:

                        sprintf ( typeStr, "16:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[WORD] Reading RAM aligned...\n", testStr );

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0, add = startAdd ; add < length - 2; n += 2, add += 2 ) 
                        {
                            d16 = be16toh ( read16 (add) );
                            
                            if ( d16 != *( (uint16_t *) &garbagePtr [n] ) )
                            {
                                if ( errors < 10 )
                                {
                                    if (errors == 0)
                                        printf ( "\n" );

                                    printf ( "%sData mismatch at $%.6X: %.04X should be %.04X\n", testStr, add, d16, (uint16_t) garbagePtr [n + 1] << 8 | garbagePtr [n] );
                                }

                                errors++;
                            }

                            if ( n % (length / 64)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_16_ODD:

                        sprintf ( typeStr, "16_ODD:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[WORD] Reading RAM unaligned...\n", testStr );

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 1, add = startAdd + 1; add < length - 2; n += 2, add += 2 ) 
                        {
                            d16 = be16toh ( (read8 (add) << 8) | read8 (add + 1) );
                            
                            if ( d16 != *( (uint16_t *) &garbagePtr [n] ) )
                            {
                                if ( errors < 10 )
                                {
                                    if (errors == 0)
                                        printf ( "\n" );

                                    printf ( "%sData mismatch at $%.6X: %.04X should be %.04X\n", testStr, add, d16, *( (uint16_t *) &garbagePtr [n] ) );
                                }

                                errors++;
                            }

                            if ( !errors )
                            {
                                if ( (n - 1) % (length / 64)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_32:

                        sprintf ( typeStr, "32:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[LONG] Reading RAM aligned...\n", testStr );

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0, add = startAdd; add < length - 4; n += 4, add += 4 ) 
                        {
                            d32 = be32toh ( read32 (add) );
                            
                            if ( d32 != *( (uint32_t *) &garbagePtr [n] ) )
                            {
                                if ( errors < 10 )
                                {
                                    if (errors == 0)
                                        printf ( "\n" );

                                    printf ( "%sData mismatch at $%.6X: %.08X should be %.08X\n", testStr, add, d32, *( (uint32_t *) &garbagePtr [n] ) );
                                }

                                errors++;
                            }

                            if ( !errors )
                            {
                                if ( n % (length / 64)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_32_ODD:

                        sprintf ( typeStr, "32_ODD:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[LONG] Reading RAM unaligned...\n", testStr );

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 1, add = startAdd + 1; add < length - 4; n += 4, add += 4 ) 
                        {
                            d32 = read8 (add);
                            d32 |= (be16toh ( read16 (add + 1) ) << 8);
                            d32 |= (read8 (add + 3) << 24 );
                            
                            if ( d32 != *( (uint32_t *) &garbagePtr [n] ) )
                            {
                                if ( errors < 10 )
                                {
                                    if (errors == 0)
                                        printf ( "\n" );

                                    printf ( "%sData mismatch at $%.6X: %.08X should be %.08X\n", testStr, add, d32, *( (uint32_t *) &garbagePtr [n] ) );
                                }

                                errors++;
                            }

                            if ( !errors )
                            {
                                if ( (n - 1) % (length / 64)  == 0 ) 
                                {
                                    printf ( "." );
                                    fflush ( stdout );
                                }
                            }
                        }

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

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0, add = startAdd ; add < length; n++, add++ ) 
                        {
                            
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
                                if ( errors < 10 )
                                {
                                    if (errors == 0)
                                        printf ( "\n" );

                                    printf ( "%sData mismatch at $%.6X: %.02X should be %.02X\n", testStr, radd, d8, rd8 );
                                }

                                errors++;
                            }

                            /* sanity feedback - one dot per 64 KB */
                            if ( n % (length / 64)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }

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
                                if ( errors < 10 )
                                {
                                    if (errors == 0)
                                        printf ( "\n" );

                                    printf ( "%sData mismatch at $%.6X: %.04X should be %.04X\n", testStr, radd, d16, rd16 );
                                }

                                errors++;
                            }

                            /* sanity feedback - one dot per 64 KB */
                            if ( n % (length / 64)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }

                        clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                        printf ( "\n" );

                    break;

                    case TEST_32_RANDOM:

                        srand (length);

                        sprintf ( typeStr, "32_RANDOM_RW:" );
                        sprintf ( testStr, "%s%s ", dirStr, typeStr );

                        printf ( "Test %d\n", testNumber );
                        printf ( "%-20s[LONG] Writing random data to random addresses aligned...\n", testStr );

                        clock_gettime ( CLOCK_REALTIME, &tmsStart );

                        for ( uint32_t n = 0, add = startAdd ; add < length - 4; n += 4, add += 4 ) 
                        {
                            
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
                                if ( errors < 10 )
                                {
                                    if (errors == 0)
                                        printf ( "\n" );

                                    printf ( "%sData mismatch at $%.6X: %.08X should be %.08X\n", testStr, radd, d32, rd32 );
                                }

                                errors++;
                            }

                            /* sanity feedback - one dot per 64 KB */
                            if ( n % (length / 64)  == 0 ) 
                            {
                                printf ( "." );
                                fflush ( stdout );
                            }
                        }

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

                    for ( uint32_t n = 0, add = startAdd; add < length; n++, add++ ) 
                    {
                        d8 = garbagePtr [n];

                        write8 ( add, d8 );
                        rd8 = read8  ( add );
                        
                        if ( d8 != rd8 ) 
                        {
                            if ( errors < 10 )
                            {
                                if (errors == 0)
                                    printf ( "\n" );

                                printf ( "%sData mismatch at $%.6X: %.02X should be %.02X\n", testStr, add, rd8, d8 );
                            }

                            errors++;
                        }

                        if ( n % (length / 64)  == 0 ) 
                        {
                            printf ( "." );
                            fflush ( stdout );
                        }
                    }

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;

                case TEST_16:

                    sprintf ( typeStr, "16:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[WORD] Writing to RAM aligned... \n", testStr );

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    for ( uint32_t n = 0, add = startAdd; add < length - 2; n += 2, add += 2) 
                    {
                        d16 = *( (uint16_t *) &garbagePtr [n] );

                        write16 ( add, d16 );
                        rd16 = read16  ( add );
                        
                        if ( d16 != rd16 ) 
                        {
                            if ( errors < 10 )
                            {
                                if (errors == 0)
                                    printf ( "\n" );

                                printf ( "%sData mismatch at $%.6X: %.04X should be %.04X\n", testStr, add, rd16, d16 );
                            }

                            errors++;
                        }

                        if ( n % (length / 64)  == 0 ) 
                        {
                            printf ( "." );
                            fflush ( stdout );
                        }
                    }

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;

                case TEST_16_ODD:

                    sprintf ( typeStr, "16_ODD:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[WORD] Writing to RAM unaligned... \n", testStr );

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    for ( uint32_t n = 1, add = startAdd + 1; add < length - 2; n += 2, add += 2) 
                    {
                        d16 = *( (uint16_t *) &garbagePtr [n] );

                        write8 ( add, (d16 & 0x00FF) );
                        write8 ( add + 1, (d16 >> 8) );                      
                        rd16 = be16toh ( (read8 (add) << 8) | read8 (add + 1) );

                        if ( d16 != rd16 ) 
                        {
                            if ( errors < 10 )
                            {
                                if (errors == 0)
                                    printf ( "\n" );

                                printf ( "%sData mismatch at $%.6X: %.04X should be %.04X\n", testStr, add, rd16, d16 );
                            }

                            errors++;
                        }

                        if ( (n - 1) % (length / 64)  == 0 ) 
                        {
                            printf ( "." );
                            fflush ( stdout );
                        }
                    }

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;

                case TEST_32:

                    sprintf ( typeStr, "32:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[LONG] Writing to RAM aligned... \n", testStr );

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    for ( uint32_t n = 0, add = startAdd; add < length - 4; n += 4, add += 4) 
                    {
                        d32 = *( (uint32_t *) &garbagePtr [n] );

                        write32 ( add, d32 );
                        rd32 = read32 ( add );

                        if ( d32 != rd32 ) 
                        {
                            if ( errors < 10 )
                            {
                                if (errors == 0)
                                    printf ( "\n" );

                                printf ( "%sData mismatch at $%.6X: %.08X should be %.08X\n", testStr, add, rd32, d32 );
                            }

                            errors++;
                        }

                        if ( n % (length / 64)  == 0 ) 
                        {
                            printf ( "." );
                            fflush ( stdout );
                        }
                    }

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;

                case TEST_32_ODD:

                    sprintf ( typeStr, "32_ODD:" );
                    sprintf ( testStr, "%s%s ", dirStr, typeStr );

                    printf ( "Test %d\n", testNumber );
                    printf ( "%-20s[LONG] Writing to RAM unaligned... \n", testStr );

                    clock_gettime ( CLOCK_REALTIME, &tmsStart );

                    for ( uint32_t n = 1, add = startAdd + 1; add < length - 4; n += 4, add += 4) 
                    {
                        d32 = *( (uint32_t *) &garbagePtr [n] );

                        write8  ( add, (d32 & 0x0000FF) );
                        write16 ( add + 1, htobe16 ( ( (d32 & 0x00FFFF00) >> 8) ) );
                        write8  ( add + 3, (d32 & 0xFF000000) >> 24);

                        rd32  = read8 (add);
                        rd32 |= (be16toh ( read16 (add + 1) ) << 8);
                        rd32 |= (read8 (add + 3) << 24 );

                        if ( d32 != rd32 ) 
                        {
                            if ( errors < 10 )
                            {
                                if (errors == 0)
                                    printf ( "\n" );

                                printf ( "%sData mismatch at $%.6X: %.08X should be %.08X\n", testStr, add, rd32, d32 );
                            }

                            errors++;
                        }

                        if ( (n - 1) % (length / 64)  == 0 ) 
                        {
                            printf ( "." );
                            fflush ( stdout );
                        }
                    }

                    clock_gettime ( CLOCK_REALTIME, &tmsEnd );

                    printf ( "\n" );

                break;
            }
                

        break;
    }

    nanoStart = (tmsStart.tv_sec * 1000) + (tmsStart.tv_nsec / 1000000);
    nanoEnd   = (tmsEnd.tv_sec * 1000) + (tmsEnd.tv_nsec / 1000000);

    totalErrors += errors;
    testNumber++;

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

    


    printf ( "%-20sCompleted %sin %d ms (%.2f KB/s)\nTotal errors = %d\n\n", 
        testStr, 
        errors ? "with errors " : "",
        (nanoEnd - nanoStart), 
        (( (float)calcLength / (float)(nanoEnd - nanoStart)) * 1000.0) / 1024,     /* KB/s */
        totalErrors );
}


void clearmem ( uint32_t length, uint32_t *duration )
{
    struct timespec tmsStart, tmsEnd;


    clock_gettime ( CLOCK_REALTIME, &tmsStart );
    
    for ( uint32_t n = 0; n < length; n += 2 ) {

        write16 (n, 0x0000);

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
        in = read8 ( ROMaddress + i) ;

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
    char *tptr;

    
    for ( int c = 1; c < argc; c++ )
    {
        if ( strcmp ( argv [c], "--memory" ) == 0 )
        {        

            for ( int n = c + 1; n < c + 4 && n < argc; n++ )
            {
                memset ( arguments, 0, sizeof (arguments) );
                strncpy ( arguments, argv [n], strlen (argv [n]) );

                tptr = strtok ( arguments, "=" );
        
                if ( strcmp ( tptr, "tests" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );

                    if ( *tptr == 'r' || *(tptr + 1) == 'r' || *(tptr + 2) == 'r'  )
                        doReads = 1;

                    if ( *tptr == 'w' || *(tptr + 1) == 'w' || *(tptr + 2) == 'w'  )
                        doWrites = 1;

                    if ( *tptr == 'x' || *(tptr + 1) == 'x' || *(tptr + 2) == 'x'  )
                        doRandoms = 1;
                }

                else if ( strcmp ( tptr, "size" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
            
                    testSize = atoi (tptr);

                    if ( testSize < 512 )
                        testSize = 512;

                    if ( testSize > 4096 )
                        testSize = 4096;
                }

                else if ( strcmp ( tptr, "loop" ) == 0 )
                {   
                    tptr = strtok ( NULL, "" );

                    if ( strcmp ( tptr, "yes" ) == 0 )
                        loopTests = 1;
                }
            }

            cmdMem = 1;
            valid = 1;
        }

        else if ( strcmp ( argv [c], "--clearmem" ) == 0 )
        {
            if ( argv [c + 1] != NULL )
            {
                sscanf ( argv [c + 1], "%d", &testSize );

                if ( testSize < 512 )
                    testSize = 512;

                if ( testSize > 4096 )
                    testSize = 4096;

                cmdClear = 1;
                valid = 1;
            }           
        }

        else if ( strcmp ( argv [c], "--peek" ) == 0 )
        {
            if ( argv [c + 1] != NULL )
            {
                sscanf ( argv [c + 1], "%x", &padd );

                cmdPeek = 1;
                valid = 1;
            }
        }

        /* syntax --dumprom size=256 address=0xe00000 */
        else if ( strcmp ( argv [c], "--dumprom" ) == 0 )
        {
            for ( int n = c + 1; n < c + 3 && n < argc; n++ )
            {
                memset ( arguments, 0, sizeof (arguments) );
                strncpy ( arguments, argv [n], strlen (argv [n]) );

                tptr = strtok ( arguments, "=" );
        
                if ( strcmp ( tptr, "address" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );

                    sscanf ( tptr, "%X", &ROMaddress );
                }

                else if ( strcmp ( tptr, "size" ) == 0 )
                {
                    tptr = strtok ( NULL, "" );
            
                    ROMsize = atoi (tptr);

                    if ( ROMsize > 256 || ROMsize < 192 )
                        ROMsize = 192;
                }
            }

            cmdDump = 1;
            valid = 1;
        }

        if ( strcmp ( argv [c], "--init" ) == 0 )
        {
            memSize = atoi ( argv [c + 1] );

            if ( memSize < 512 )
                memSize = 512;

            if ( memSize > 4096 )
                memSize = 4096;
                
            valid = 1;     
        }
    }

    return valid | syntax;
}