# Cross-build MuGreen2 for AArch64 or 32-bit ARM hard-float.
ARCH ?= aarch64
ifeq ($(ARCH),aarch64)
CROSS_COMPILE ?= /opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
else ifeq ($(ARCH),armhf)
CROSS_COMPILE ?= arm-linux-gnueabihf-
else
$(error Unsupported ARCH '$(ARCH)' (choose aarch64 or armhf))
endif
SYSROOT ?= $(abspath $(dir $(CROSS_COMPILE))/../sysroot)
MUPDF ?= /opt/mupdf-$(ARCH)
PREFIX ?= /usr/local
SDL2_CONFIG ?= sdl2-config

ifeq ($(ARCH),aarch64)
ARCHFLAGS ?= -mcpu=cortex-a53+crc -mtune=cortex-a53
else ifeq ($(ARCH),armhf)
ARCHFLAGS ?= -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
else
$(error Unsupported ARCH '$(ARCH)' (choose aarch64 or armhf))
endif

CC := $(CROSS_COMPILE)gcc --sysroot=$(SYSROOT)
STRIP := $(CROSS_COMPILE)strip
CONFIG := -D GREEN_SYSCONFIG_FILE='"$(PREFIX)/etc/green.conf"' -D GREEN_USERCONFIG_FILE='".green.conf"'
SDL2_CFLAGS ?= $(shell $(SDL2_CONFIG) --cflags)
SDL2_LIBS ?= $(shell $(SDL2_CONFIG) --libs)
CFLAGS := -O2 -Wall $(ARCHFLAGS) -flto -ffunction-sections -fdata-sections -D_REENTRANT -I. -I$(MUPDF)/include $(SDL2_CFLAGS)
LDFLAGS := $(ARCHFLAGS) -flto -Wl,--gc-sections -L$(MUPDF)/lib -L$(SYSROOT)/usr/lib
LIBS := -lmupdf -lmupdf-third $(SDL2_LIBS) -lm
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

.PHONY: all clean
