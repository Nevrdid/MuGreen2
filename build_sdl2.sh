#!/bin/sh
# Build SDL2 for AArch64, ARMHF, or the native host.
set -eu

SDL2_VER=${SDL2_VER:-2.30.8}
ARCH=${ARCH:-aarch64}

case "$ARCH" in
 native)
  # A native build must not inherit a cross-compiler from the environment.
  unset CROSS_COMPILE
  ;;
 aarch64|armhf)
  if [ -z "${CROSS_COMPILE:-}" ]; then
   case "$ARCH" in
    aarch64)
     CROSS_COMPILE=/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-
     if ! command -v "${CROSS_COMPILE}gcc" >/dev/null 2>&1; then
      if command -v aarch64-linux-gnu-gcc >/dev/null 2>&1; then
       CROSS_COMPILE=aarch64-linux-gnu-
      else
       echo "AArch64 compiler not found: install the /opt toolchain or aarch64-linux-gnu-gcc." >&2
       exit 1
      fi
     fi
     ;;
    armhf) CROSS_COMPILE=arm-linux-gnueabihf- ;;
   esac
  fi
  export CROSS_COMPILE
  ;;
 *) echo "Unsupported ARCH=$ARCH (choose aarch64, armhf, or native)." >&2; exit 1 ;;
esac

CC_BASE="${CROSS_COMPILE:-}gcc"
if [ "${SYSROOT+x}" != x ]; then
 SYSROOT=$("$CC_BASE" -print-sysroot 2>/dev/null || true)
 if [ "$SYSROOT" = "/" ]; then SYSROOT=; fi
fi
SYSROOT=${SYSROOT-}
PREFIX=${PREFIX:-$PWD/sdl2/$ARCH}
WORK=${WORK:-$PWD/sdl2-build}
CLEAN=${CLEAN:-1}

case "$ARCH" in
 aarch64) ARCHFLAGS=${ARCHFLAGS:--mcpu=cortex-a53+crc -mtune=cortex-a53} ;;
 armhf) ARCHFLAGS=${ARCHFLAGS:--march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard} ;;
 native) ARCHFLAGS=${ARCHFLAGS-} ;;
esac

SYSROOT_FLAG=
if [ -n "$SYSROOT" ]; then SYSROOT_FLAG="--sysroot=$SYSROOT"; fi
CC="$CC_BASE $SYSROOT_FLAG"
CXX="${CROSS_COMPILE:-}g++ $SYSROOT_FLAG"
AR="${CROSS_COMPILE:-}ar"
RANLIB="${CROSS_COMPILE:-}ranlib"
STRIP="${CROSS_COMPILE:-}strip"
HOST=$("$CC_BASE" -dumpmachine)
BUILD=$(gcc -dumpmachine)

mkdir -p "$WORK"
cd "$WORK"
ARCHIVE="SDL2-$SDL2_VER.tar.gz"
SOURCE="SDL2-$SDL2_VER"

if [ ! -d "$SOURCE" ]; then
 [ -f "$ARCHIVE" ] || curl -fL "https://www.libsdl.org/release/$ARCHIVE" -o "$ARCHIVE"
 tar xzf "$ARCHIVE"
fi

cd "$SOURCE"
if [ "$CLEAN" = 1 ]; then
 make distclean >/dev/null 2>&1 || true
fi

CONFIGURE_ARGS="--prefix=$PREFIX --disable-rpath"
if [ "$HOST" != "$BUILD" ]; then
 CONFIGURE_ARGS="$CONFIGURE_ARGS --build=$BUILD --host=$HOST"
fi

# Keep SDL2's architecture-specific install prefix separate from other builds.
CFLAGS="${CFLAGS:-} $ARCHFLAGS"
CXXFLAGS="${CXXFLAGS:-} $ARCHFLAGS"
export CC CXX AR RANLIB STRIP CFLAGS CXXFLAGS

# Intentional word splitting for configure's option list.
# shellcheck disable=SC2086
./configure $CONFIGURE_ARGS
make -j"$(nproc)"
make install

echo "SDL2 $SDL2_VER ($ARCH) installed to $PREFIX"
