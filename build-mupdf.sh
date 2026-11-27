#!/bin/sh
# Cross-compile a trimmed static MuPDF for ARCH=aarch64 or ARCH=armhf.
set -eu
MUPDF_VER=${MUPDF_VER:-1.24.11}
ARCH=${ARCH:-aarch64}
if [ "${CROSS_COMPILE+x}" != x ]; then
 case "$ARCH" in
  aarch64) CROSS_COMPILE=/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu- ;;
  armhf) CROSS_COMPILE=arm-linux-gnueabihf- ;;
  native) CROSS_COMPILE= ;;
  *) echo "Unsupported ARCH=$ARCH (choose aarch64, armhf, or native)." >&2; exit 1 ;;
 esac
fi
CC_BASE="${CROSS_COMPILE}gcc"
if [ "${SYSROOT+x}" != x ]; then
 SYSROOT=$("$CC_BASE" -print-sysroot 2>/dev/null || true)
 if [ "$SYSROOT" = "/" ]; then SYSROOT=; fi
fi
SYSROOT=${SYSROOT-}
PREFIX=${PREFIX:-/opt/mupdf-${ARCH}}
WORK=${WORK:-$PWD/mupdf-build}
CLEAN=${CLEAN:-1}
case "$ARCH" in
 aarch64) ARCHFLAGS=${ARCHFLAGS:--mcpu=cortex-a53+crc -mtune=cortex-a53}; EXPECTED_FORMAT=${EXPECTED_FORMAT:-aarch64} ;;
 armhf) ARCHFLAGS=${ARCHFLAGS:--march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard}; EXPECTED_FORMAT=${EXPECTED_FORMAT:-elf32-littlearm} ;;
 native) ARCHFLAGS=${ARCHFLAGS-}; EXPECTED_FORMAT= ;;
 *) echo "Unsupported ARCH=$ARCH (choose aarch64, armhf, or native)." >&2; exit 1 ;;
esac
case "$PREFIX" in
 "$SYSROOT"|"$SYSROOT"/usr|"$SYSROOT"/usr/)
  if [ "${ALLOW_SYSROOT_PREFIX:-0}" != 1 ]; then echo "PREFIX is inside the sysroot; use a separate prefix." >&2; exit 1; fi ;;
esac
SYSROOT_FLAG=
if [ -n "$SYSROOT" ]; then SYSROOT_FLAG="--sysroot=$SYSROOT"; fi
CC="$CC_BASE $SYSROOT_FLAG"
CXX="${CROSS_COMPILE}g++ $SYSROOT_FLAG"
AR="${CROSS_COMPILE}ar"; RANLIB="${CROSS_COMPILE}ranlib"; LD="${CROSS_COMPILE}ld"
OBJCOPY="${CROSS_COMPILE}objcopy"; OBJDUMP="${CROSS_COMPILE}objdump"
mkdir -p "$WORK"; cd "$WORK"
if [ ! -d "mupdf-$MUPDF_VER-source" ]; then
 ARCHIVE="mupdf-$MUPDF_VER-source.tar.gz"
 [ -f "$ARCHIVE" ] || curl -fLO "https://mupdf.com/downloads/archive/$ARCHIVE"
 case "$MUPDF_VER" in
  1.24.11) DEFAULT_MUPDF_SHA256=191227b96775f6705ef7d9551187511932b519273b34535a331491cf7d98163f ;;
  *) DEFAULT_MUPDF_SHA256= ;;
 esac
 MUPDF_SHA256=${MUPDF_SHA256:-$DEFAULT_MUPDF_SHA256}
 if [ -z "$MUPDF_SHA256" ]; then
  echo "No SHA-256 is configured for MuPDF $MUPDF_VER; set MUPDF_SHA256." >&2
  exit 1
 fi
 if command -v sha256sum >/dev/null 2>&1; then
  printf '%s  %s\n' "$MUPDF_SHA256" "$ARCHIVE" | sha256sum -c -
 elif command -v shasum >/dev/null 2>&1; then
  printf '%s  %s\n' "$MUPDF_SHA256" "$ARCHIVE" | shasum -a 256 -c -
 else
  echo "sha256sum or shasum is required to verify the MuPDF archive." >&2
  exit 1
 fi
 tar xzf "$ARCHIVE"
fi
cd "mupdf-$MUPDF_VER-source"
FEATURES="-DFZ_ENABLE_XPS=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_HTML=0 -DFZ_ENABLE_EPUB=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_ICC=0 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0"
FONTS="-DTOFU -DTOFU_CJK -DTOFU_EMOJI -DTOFU_HISTORIC -DTOFU_SYMBOL -DTOFU_SIL"
OPT="-O2 -ffunction-sections -fdata-sections $ARCHFLAGS"
[ "$CLEAN" = 1 ] && rm -rf build/release
make -j"$(nproc)" build=release OS=Linux CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" LD="$LD" OBJCOPY="$OBJCOPY" HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no HAVE_LIBCRYPTO=no USE_SYSTEM_LIBS=no XCFLAGS="$OPT $FEATURES $FONTS" libs
if [ -n "$EXPECTED_FORMAT" ]; then
 BAD=$("$OBJDUMP" -a build/release/libmupdf.a 2>/dev/null | grep "file format" | grep -vc "$EXPECTED_FORMAT" || true)
 if [ "$BAD" != 0 ]; then echo "ERROR: libmupdf.a has $BAD objects not matching $EXPECTED_FORMAT." >&2; exit 1; fi
fi
mkdir -p "$PREFIX/include" "$PREFIX/lib"
cp -r include/mupdf "$PREFIX/include/"
cp build/release/libmupdf.a build/release/libmupdf-third.a "$PREFIX/lib/"
echo "MuPDF $MUPDF_VER ($ARCH) installed to $PREFIX"
