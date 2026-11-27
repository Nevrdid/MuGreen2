#!/bin/sh
# Build a shared SDL2 for AArch64, ARMHF, or the native host.
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
SDL2_PREFIX=${SDL2_PREFIX:-$PWD/sdl2/$ARCH}
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

# Keep pkg-config from accidentally selecting host libraries during a
# cross-build. PKG_CONFIG_LIBDIR remains overridable for unusual sysroots.
if [ -n "$SYSROOT" ]; then
 PKG_CONFIG_SYSROOT_DIR=${PKG_CONFIG_SYSROOT_DIR:-$SYSROOT}
 if [ "${PKG_CONFIG_LIBDIR+x}" != x ]; then
  MULTIARCH=$("$CC_BASE" -print-multiarch 2>/dev/null || true)
  PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/pkgconfig:$SYSROOT/usr/lib64/pkgconfig:$SYSROOT/usr/share/pkgconfig"
  if [ -n "$MULTIARCH" ]; then
   PKG_CONFIG_LIBDIR="$SYSROOT/usr/lib/$MULTIARCH/pkgconfig:$PKG_CONFIG_LIBDIR"
  fi
 fi
 export PKG_CONFIG_SYSROOT_DIR PKG_CONFIG_LIBDIR
fi

mkdir -p "$WORK"
cd "$WORK"
ARCHIVE="SDL2-$SDL2_VER.tar.gz"
SOURCE="SDL2-$SDL2_VER"

if [ ! -d "$SOURCE" ]; then
 [ -f "$ARCHIVE" ] || curl -fL "https://www.libsdl.org/release/$ARCHIVE" -o "$ARCHIVE"

 case "$SDL2_VER" in
  2.30.8) DEFAULT_SDL2_SHA256=380c295ea76b9bd72d90075793971c8bcb232ba0a69a9b14da4ae8f603350058 ;;
  *) DEFAULT_SDL2_SHA256= ;;
 esac
 SDL2_SHA256=${SDL2_SHA256:-$DEFAULT_SDL2_SHA256}
 if [ -z "$SDL2_SHA256" ]; then
  echo "No SHA-256 is configured for SDL2 $SDL2_VER; set SDL2_SHA256." >&2
  exit 1
 fi

 if command -v sha256sum >/dev/null 2>&1; then
  if ! printf '%s  %s\n' "$SDL2_SHA256" "$ARCHIVE" | sha256sum -c -; then
   echo "Removing the invalid SDL2 archive; it will be downloaded again next time." >&2
   rm -f "$ARCHIVE"
   exit 1
  fi
 elif command -v shasum >/dev/null 2>&1; then
  if ! printf '%s  %s\n' "$SDL2_SHA256" "$ARCHIVE" | shasum -a 256 -c -; then
   echo "Removing the invalid SDL2 archive; it will be downloaded again next time." >&2
   rm -f "$ARCHIVE"
   exit 1
  fi
 else
  echo "sha256sum or shasum is required to verify the SDL2 archive." >&2
  exit 1
 fi

 tar xzf "$ARCHIVE"
fi

cd "$SOURCE"
if [ "$CLEAN" = 1 ]; then
 make distclean >/dev/null 2>&1 || true
fi

CONFIGURE_ARGS="--prefix=$SDL2_PREFIX --disable-rpath --enable-shared --disable-static"
if [ "$HOST" != "$BUILD" ]; then
 CONFIGURE_ARGS="$CONFIGURE_ARGS --build=$BUILD --host=$HOST"
fi

# The handheld targets use KMS/DRM + EGL/GLES2 only. Do not let missing
# sysroot packages silently turn this into a software-only or desktop build.
case "$ARCH" in
 aarch64|armhf)
  CONFIGURE_ARGS="$CONFIGURE_ARGS \
   --enable-video-kmsdrm --disable-kmsdrm-shared \
   --enable-video-opengles --enable-video-opengles2 --disable-video-opengles1 --disable-video-opengl \
   --disable-video-dummy --disable-video-offscreen --disable-video-x11 --disable-video-wayland --disable-video-directfb \
   --disable-video-vulkan --disable-pulseaudio --disable-pipewire \
   --disable-alsa --disable-oss --disable-jack --disable-sndio --disable-audio"
  ;;
 native)
  # Retain the host's available desktop drivers for native development builds.
  ;;
esac

CFLAGS="${CFLAGS:-} $ARCHFLAGS"
CXXFLAGS="${CXXFLAGS:-} $ARCHFLAGS"
export CC CXX AR RANLIB STRIP CFLAGS CXXFLAGS

# Intentional word splitting for configure's option list.
# shellcheck disable=SC2086
./configure $CONFIGURE_ARGS

if [ "$ARCH" != native ]; then
 for macro in SDL_VIDEO_DRIVER_KMSDRM SDL_VIDEO_OPENGL_EGL SDL_VIDEO_RENDER_OGL_ES2; do
  if ! grep -q "^#define $macro 1$" include/SDL_config.h; then
   echo "Required SDL2 feature $macro was not configured." >&2
   echo "Check that the target sysroot provides libdrm, libgbm, EGL and GLES2 headers/pkg-config files." >&2
   exit 1
  fi
 done
fi

make -j"$(nproc)"
make install

echo "SDL2 $SDL2_VER ($ARCH) installed to $SDL2_PREFIX"
echo "Configured SDL2 video drivers:"
if ! sed -n 's/^#define SDL_VIDEO_DRIVER_\([A-Z0-9_]*\) 1$/  \1/p' include/SDL_config.h | tee /dev/stderr | grep -q .; then
 echo "  (none found)"
fi
echo "Configured SDL2 renderer backends:"
if ! sed -n 's/^#define SDL_VIDEO_RENDER_\([A-Z0-9_]*\) 1$/  \1/p' include/SDL_config.h | tee /dev/stderr | grep -q .; then
 echo "  (none found)"
fi
