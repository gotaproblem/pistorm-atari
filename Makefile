EXENAME          = emulator

MAINFILES        = emulator.c \
				memory_mapped.c \
				config_file/config_file.c \
				gpio/ps_protocol.c \
				platforms/platforms.c \
				platforms/atari/atari-autoconf.c \
				platforms/atari/atari-platform.c \
				platforms/atari/atari-registers.c \
				platforms/atari/IDE.c \
				platforms/atari/idedriver.c \
				platforms/dummy/dummy-platform.c \
				platforms/dummy/dummy-registers.c \
				platforms/atari/rtg.c
#				platforms/atari/et4000.c


MUSASHIFILES     = m68kcpu.c m68kdasm.c softfloat/softfloat.c softfloat/softfloat_fpsp.c
MUSASHIGENCFILES = m68kops.c
MUSASHIGENHFILES = m68kops.h
MUSASHIGENERATOR = m68kmake

.CFILES   = $(MAINFILES) $(MUSASHIFILES) $(MUSASHIGENCFILES)
.OFILES   = $(.CFILES:%.c=%.o)

CC        = gcc

#RAYLIB    = -I./raylib-test -L./raylib-test -lraylib -lGLESv2 -lEGL -ldrm -lgbm -lm

PIOPTS	  = -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8

ifeq ($(PIMODEL),PI3)
	PI = -DPI3
else
	PI = -DPI4
endif

ifeq ($(CACHE),ON)
	STRAMCACHE = -DSTRAMCACHE=1
else
	STRAMCACHE =
endif

#ifeq ($(USERAYLIB),YES)
#	CFLAGS = -I. $(PI4OPTS) -O3 $(RAYLIB) -DRTG -DRAYLIB #-DT_CACHE_ON -DCACHE_ON
#else
#	CFLAGS = -I. $(PI4OPTS) -O3 #-DT_CACHE_ON -DCACHE_ON
#endif

CFLAGS    = -I. $(PIOPTS) -O3 $(PI) $(STRAMCACHE)
TARGET    = $(EXENAME)

DELETEFILES = $(MUSASHIGENCFILES) $(.OFILES) $(.OFILES:%.o=%.d) $(TARGET) $(MUSASHIGENERATOR) ataritest

all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) ataritest

clean:
	rm -f $(DELETEFILES)

$(TARGET):  $(MUSAHIGENCFILES:%.c=%.o) $(.CFILES:%.c=%.o)
	$(CC) -o $@ $^ $(CFLAGS) -pthread

ataritest: ataritest.c gpio/ps_protocol.c
	$(CC) $^ -o $@ $(CFLAGS)

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR) m68kcpu.h
	./$(MUSASHIGENERATOR)

$(MUSASHIGENERATOR):  $(MUSASHIGENERATOR).c
	$(CC) -o  $(MUSASHIGENERATOR)  $(MUSASHIGENERATOR).c

-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) $(MUSASHIGENERATOR).d