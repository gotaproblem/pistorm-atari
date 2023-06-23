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
	platforms/atari/blitter.c 
#	platforms/atari/pistorm-dev/pistorm-dev.c
#	platforms/atari/hunk-reloc.c \
#	platforms/atari/piscsi/piscsi.c \
#	platforms/shared/rtc.c 
#	config_file/rominfo.c \
#	platforms/shared/common.c \
#	platforms/dummy/dummy-platform.c \
#	platforms/dummy/dummy-registers.c \

MUSASHIFILES     = m68kcpu.c m68kdasm.c softfloat/softfloat.c softfloat/softfloat_fpsp.c
MUSASHIGENCFILES = m68kops.c
MUSASHIGENHFILES = m68kops.h
MUSASHIGENERATOR = m68kmake

# EXE = .exe
# EXEPATH = .\\
#EXE =
EXEPATH = ./

.CFILES   = $(MAINFILES) $(MUSASHIFILES) $(MUSASHIGENCFILES)
.OFILES   = $(.CFILES:%.c=%.o)
#.OFILES   = $(.CFILES:%.c=%.o) a314/a314.o

CC        = gcc
#CXX       = g++
#WARNINGS  = -Wall -Wextra -pedantic
WARNINGS  =
#PI3OPTS   = -mfpu=crypto-neon-fp-armv8 -mfloat-abi=hard -march=armv8-a+crc -mcpu=cortex-a53
#PI4OPTS	  = -mfpu=neon-fp-armv8 -mfloat-abi=hard -march=armv8-a+crc -mcpu=cortex-a72
PI4OPTS	  = -mcpu=cortex-a72
#ORIG	  = -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8

#ifeq ($(PLATFORM),PI3_BULLSEYE)
# LFLAGS    = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib -L./raylib_drm -lraylib -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound
# CFLAGS    = $(WARNINGS) -I. -I./raylib -I/opt/vc/include/ -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -lstdc++ $(ACFLAGS)
#	LFLAGS    = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib  -lGLESv2 -lEGL -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif -lasound
#	CFLAGS    = $(WARNINGS) -I. -I/opt/vc/include/ -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -lstdc++ $(ACFLAGS)
#else ifeq ($(PLATFORM),PI4)
#	LFLAGS    = $(WARNINGS) -L/usr/local/lib -L/opt/vc/lib -lgbm -ldrm -ldl -lstdc++ -lvcos -lvchiq_arm -lvchostif #-lasound
	LFLAGS    = $(WARNINGS) -lstdc++
#	CFLAGS    = $(WARNINGS) -DRPI4_TEST -I. -I/opt/vc/include/ $(PI4OPTS) -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -lstdc++ $(ACFLAGS)
	CFLAGS    = $(WARNINGS) -I. $(PI4OPTS) -O2 -lstdc++
#else
#	CFLAGS    = $(WARNINGS) -I. -I./raylib -I/opt/vc/include/ -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -lstdc++ $(ACFLAGS)
#	LFLAGS    = $(WARNINGS) -L/opt/vc/lib -L./raylib -lraylib -lbrcmGLESv2 -lbrcmEGL -lbcm_host -lstdc++ -lvcos -lvchiq_arm -lasound
#endif

TARGET = $(EXENAME)$(EXE)

DELETEFILES = $(MUSASHIGENCFILES) $(.OFILES) $(.OFILES:%.o=%.d) $(TARGET) $(MUSASHIGENERATOR)$(EXE)


all: $(MUSASHIGENCFILES) $(MUSASHIGENHFILES) $(TARGET) #ataritest #zz9fulltest zz9readloop

clean:
	rm -f $(DELETEFILES)

# $(TARGET):  $(MUSAHIGENCFILES:%.c=%.o) $(.CFILES:%.c=%.o) a314/a314.o
$(TARGET):  $(MUSAHIGENCFILES:%.c=%.o) $(.CFILES:%.c=%.o)
#	$(CC) -o $@ $^ -O3 -pthread $(LFLAGS) -lm -lstdc++
	$(CC) -o $@ $^ -O2 $(LFLAGS) -mcpu=cortex-a72

#buptest: buptest.c gpio/ps_protocol.c
#	$(CC) $^ -o $@ -I./ -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O0

ataritest: ataritest.c gpio/ps_protocol.c
	$(CC) $^ -o $@ -I./ $(PI4OPTS) -O2 -mcpu=cortex-a72

#zz9fulltest: zz9fulltest.c gpio/ps_protocol.c
#	$(CC) $^ -o $@ -I./ -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O0

#zz9readloop: zz9readloop.c gpio/ps_protocol.c
#	$(CC) $^ -o $@ -I./ -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O0

#a314/a314.o: a314/a314.cc a314/a314.h
#	$(CXX) -MMD -MP -c -o a314/a314.o -O3 a314/a314.cc -march=armv8-a -mfloat-abi=hard -mfpu=neon-fp-armv8 -O3 -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -I. -I..

#makedisk: platforms/atari/makedisk.c platforms/atari/idedriver.c
#	$(CC) $^ -o $@ -I./ $(PI4OPTS) -O3

$(MUSASHIGENCFILES) $(MUSASHIGENHFILES): $(MUSASHIGENERATOR)$(EXE) m68kcpu.h
	$(EXEPATH)$(MUSASHIGENERATOR)$(EXE)

$(MUSASHIGENERATOR)$(EXE):  $(MUSASHIGENERATOR).c
	$(CC) -o  $(MUSASHIGENERATOR)$(EXE)  $(MUSASHIGENERATOR).c -mcpu=cortex-a72

# -include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) a314/a314.d $(MUSASHIGENERATOR).d
-include $(.CFILES:%.c=%.d) $(MUSASHIGENCFILES:%.c=%.d) $(MUSASHIGENERATOR).d