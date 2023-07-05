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
				platforms/atari/rtg.c

MUSASHIFILES     = m68kcpu.c m68kdasm.c softfloat/softfloat.c softfloat/softfloat_fpsp.c
MUSASHIGENCFILES = m68kops.c
MUSASHIGENHFILES = m68kops.h
MUSASHIGENERATOR = m68kmake

EXEPATH   = ./

.CFILES   = $(MAINFILES) $(MUSASHIFILES) $(MUSASHIGENCFILES)
.OFILES   = $(.CFILES:%.c=%.o)

CC        = gcc

RAYLIB    = -I./raylib_pi4_test -L./raylib_pi4_test -lraylib -lGLESv2 -lEGL -ldrm -lgbm -lm

#PI4OPTS	  = -mcpu=cortex-a72
PI4OPTS	  = -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8

CFLAGS    = -I. $(PI4OPTS) $(RAYLIB) -O3 -DRTG -DRAYLIB -DT_CACHE_ON #-DCACHE_ON

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
	$(EXEPATH)$(MUSASHIGENERATOR)

$(MUSASHIGENERATOR)$(EXE):  $(MUSASHIGENERATOR).c
	$(CC) -o  $(MUSASHIGENERATOR)  $(MUSASHIGENERATOR).c -mcpu=cortex-a72

-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) $(MUSASHIGENERATOR).d