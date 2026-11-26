# Cross-build of MuGreen2 (MuPDF backend + SDL2) for aarch64 / Cortex-A53.
#   make MUPDF=/path/to/mupdf-prefix
#   MUPDF   : prefix produced by build-mupdf.sh (include/mupdf, lib/libmupdf*.a)
TC      ?= /opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu
TRIPLET ?= aarch64-none-linux-gnu
SYSROOT ?= $(TC)/sysroot
MUPDF   ?= /opt/mupdf-aarch64
PREFIX  ?= /usr/local
SDL2_HEADS ?= `/opt/SDL2-2.30.8/build/sdl2-config --cflags`
SDL2_LIBS ?= `/opt/SDL2-2.30.8/build/sdl2-config --libs`

CC      := $(TC)/bin/$(TRIPLET)-gcc --sysroot=$(SYSROOT)
STRIP   := $(TC)/bin/$(TRIPLET)-strip
ARCH    := -mcpu=cortex-a53+crc -mtune=cortex-a53
CONFIG  := -D GREEN_SYSCONFIG_FILE=\\\"$(PREFIX)/etc/green.conf\\\" -D GREEN_USERCONFIG_FILE=\\\".green.conf\\\"
CFLAGS  := -O2 -Wall $(ARCH) -flto -ffunction-sections -fdata-sections -D_REENTRANT \\
           -I. -I$(MUPDF)/include $(SDL2_HEADS)
# Device-side glibc first (never the toolchain's newer lib64).
LDFLAGS := $(ARCH) -flto -Wl,--gc-sections -L$(MUPDF)/lib -L$(SDL2_LIBS) -L$(SYSROOT)/usr/lib
LIBS    := -lmupdf -lmupdf-third -lm
OBJS := main.o green.o sdl.o pdf_backend.o

all: mugreen2

mugreen2: $(OBJS)
	$(CC) $(LDFLAGS) $^ $(LIBS) -o $@
	$(STRIP) $@

main.o: main.c green.h pdf_backend.h
	$(CC) $(CONFIG) $(CFLAGS) -c $< -o $@

%.o: %.c green.h pdf_backend.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f mugreen2 $(OBJS)
