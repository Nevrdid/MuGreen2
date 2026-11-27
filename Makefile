# Build MuGreen2 for AArch64, 32-bit ARM hard-float, or the native host.
ARCH ?= aarch64

ifeq ($(ARCH),aarch64)
ifeq ($(origin CROSS_COMPILE),undefined)
ifneq ($(wildcard /opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-gcc),)
CROSS_COMPILE := /opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
else
CROSS_COMPILE := aarch64-linux-gnu-
endif
endif
ARCHFLAGS ?= -mcpu=cortex-a53+crc -mtune=cortex-a53
else ifeq ($(ARCH),armhf)
CROSS_COMPILE ?= arm-linux-gnueabihf-
ARCHFLAGS ?= -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
else ifeq ($(ARCH),native)
CROSS_COMPILE ?=
ARCHFLAGS ?=
else
$(error Unsupported ARCH '$(ARCH)' (choose aarch64, armhf, or native))
endif

MUPDF ?= /opt/mupdf-$(ARCH)
PREFIX ?= /usr/local
SDL2_CONFIG ?= sdl2-config

CC := $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip
# Let the selected compiler report its own sysroot. GCC prints "/" for a
# native compiler with no separate sysroot; treat that as an empty value.
SYSROOT ?= $(shell $(CC) -print-sysroot 2>/dev/null | sed 's@^/$$@@')
SYSROOT_FLAG := $(if $(strip $(SYSROOT)),--sysroot=$(SYSROOT),)
SYSROOT_LIBDIR := $(if $(strip $(SYSROOT)),-L$(SYSROOT)/usr/lib,)
SDL2_CFLAGS ?= $(shell $(SDL2_CONFIG) --cflags)
SDL2_LIBS ?= $(shell $(SDL2_CONFIG) --libs)
CFLAGS := -O2 -Wall $(ARCHFLAGS) -flto -ffunction-sections -fdata-sections -D_REENTRANT -I. -I$(MUPDF)/include $(SDL2_CFLAGS)
LDFLAGS := $(ARCHFLAGS) -flto -Wl,--gc-sections -L$(MUPDF)/lib $(SYSROOT_LIBDIR)
LIBS := -lmupdf -lmupdf-third $(SDL2_LIBS) -lm
OBJS := main.o green.o sdl.o pdf_backend.o

all: mugreen2

mugreen2: $(OBJS)
	$(CC) $(SYSROOT_FLAG) $(LDFLAGS) $^ $(LIBS) -o $@
	$(STRIP) $@

main.o: main.c green.h pdf_backend.h
	$(CC) $(SYSROOT_FLAG) $(CFLAGS) -c $< -o $@

%.o: %.c green.h pdf_backend.h
	$(CC) $(SYSROOT_FLAG) $(CFLAGS) -c $< -o $@

install: mugreen2
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 mugreen2 "$(DESTDIR)$(PREFIX)/bin/mugreen2"

clean:
	rm -f mugreen2 $(OBJS)

.PHONY: all install clean
