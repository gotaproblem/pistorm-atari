// SPDX-License-Identifier: MIT

#include "platforms.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define DEBUGPRINT 0
#if DEBUGPRINT
#define DEBUG_PRINTF(fmt, ...) fprintf(stderr, fmt, ##__VA_ARGS__)
#else
#define DEBUG_PRINTF(fmt, ...) ;
#endif

static char*platform_names[PLATFORM_NUM] = {
    "generic",
    //"amiga",
    //"mac68k",
    //"x68000",
    "ATARI"
};

int get_platform_index ( char *name ) 
{
    if ( !name || strlen ( name ) == 0 )
        return -1;

    for ( int i = 0; i < PLATFORM_NUM; i++ ) 
    {
        if ( strcmp ( name, platform_names [i] ) == 0 )
            return i;
    }

    return -1;
}

//void create_platform_amiga(struct platform_config *cfg, char *subsys);
void create_platform_atari(struct platform_config *cfg, char *subsys);
//void create_platform_mac68k(struct platform_config *cfg, char *subsys);
void create_platform_dummy(struct platform_config *cfg, char *subsys);

struct platform_config *make_platform_config(char *name, char *subsys) {
    struct platform_config *cfg = NULL;
    int platform_id = get_platform_index(name);

    if (platform_id == -1) {
        // Display a warning if no match is found for the config name, in case it was mistyped.
        DEBUG_PRINTF ("No match found for platform name \'%s\', defaulting to generic.\n", name);
        platform_id = PLATFORM_NONE;
    }
    else {
        DEBUG_PRINTF ("Creating platform config for %s...\n", name);
    }

    cfg = (struct platform_config *)malloc(sizeof(struct platform_config));

    if (!cfg) {
        DEBUG_PRINTF ("Failed to allocate memory for new platform config!.\n");
        return NULL;
    }

    memset(cfg, 0x00, sizeof(struct platform_config));

    switch(platform_id) 
    {
        //case PLATFORM_AMIGA:
        //    create_platform_amiga(cfg, subsys);
        //    break;
        //case PLATFORM_MAC:
        //    create_platform_mac68k(cfg, subsys);
        //    break;
        case PLATFORM_ATARI:
            create_platform_atari(cfg, subsys);
            break;
        case PLATFORM_NONE:
        //case PLATFORM_X68000:
        default:
            create_platform_dummy(cfg, subsys);
            break;
    }

    return cfg;
}
