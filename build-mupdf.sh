#!/bin/sh
# Cross-compile a trimmed static MuPDF for ARCH=aarch64 or ARCH=armhf.
set -eu
MUPDF_VER=${MUPDF_VER:-1.24.11}
ARCH=${ARCH:-aarch64}
CROSS_COMPILE=${CROSS_COMPILE:-/opt/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-}
SYSROOT=${SYSROOT:-${CROSS_COMPILE%/*}/../sysroot}
PREFIX=${PREFIX:-/opt/mupdf-${ARCH}}
WORK=${WORK:-$PWD/mupdf-build}
CLEAN=${CLEAN:-1}
case "$ARCH" in
 aarch64) ARCHFLAGS=${ARCHFLAGS:--mcpu=cortex-a53+crc -mtune=cortex-a53}; EXPECTED_FORMAT=${EXPECTED_FORMAT:-aarch64} ;;
 armhf) ARCHFLAGS=${ARCHFLAGS:--march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard}; EXPECTED_FORMAT=${EXPECTED_FORMAT:-elf32-littlearm} ;;
 *) echo "Unsupported ARCH=$ARCH (choose aarch64 or armhf)." >&2; exit 1 ;;
esac
case "$PREFIX" in
 "$SYSROOT"|"$SYSROOT"/usr|"$SYSROOT"/usr/)
  if [ "${ALLOW_SYSROOT_PREFIX:-0}" != 1 ]; then echo "PREFIX is inside the sysroot; use a separate prefix." >&2; exit 1; fi ;;
esac
CC="${CROSS_COMPILE}gcc --sysroot=$SYSROOT"
CXX="${CROSS_COMPILE}g++ --sysroot=$SYSROOT"
AR="${CROSS_COMPILE}ar"; RANLIB="${CROSS_COMPILE}ranlib"; LD="${CROSS_COMPILE}ld"
OBJCOPY="${CROSS_COMPILE}objcopy"; OBJDUMP="${CROSS_COMPILE}objdump"
mkdir -p "$WORK"; cd "$WORK"
if [ ! -d "mupdf-$MUPDF_VER-source" ]; then
 [ -f "mupdf-$MUPDF_VER-source.tar.gz" ] || curl -fLO "https://mupdf.com/downloads/archive/mupdf-$MUPDF_VER-source.tar.gz"
 tar xzf "mupdf-$MUPDF_VER-source.tar.gz"
fi
cd "mupdf-$MUPDF_VER-source"
FEATURES="-DFZ_ENABLE_XPS=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_HTML=0 -DFZ_ENABLE_EPUB=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_ICC=0 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0"
FONTS="-DTOFU -DTOFU_CJK -DTOFU_EMOJI -DTOFU_HISTORIC -DTOFU_SYMBOL -DTOFU_SIL"
OPT="-O2 -ffunction-sections -fdata-sections $ARCHFLAGS"
[ "$CLEAN" = 1 ] && rm -rf build/release
make -j"$(nproc)" build=release OS=Linux CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" LD="$LD" OBJCOPY="$OBJCOPY" HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no HAVE_LIBCRYPTO=no USE_SYSTEM_LIBS=no XCFLAGS="$OPT $FEATURES $FONTS" libs
BAD=$("$OBJDUMP" -a build/release/libmupdf.a 2>/dev/null | grep "file format" | grep -vc "$EXPECTED_FORMAT" || true)
if [ "$BAD" != 0 ]; then echo "ERROR: libmupdf.a has $BAD objects not matching $EXPECTED_FORMAT." >&2; exit 1; fi
mkdir -p "$PREFIX/include" "$PREFIX/lib"
cp -r include/mupdf "$PREFIX/include/"
cp build/release/libmupdf.a build/release/libmupdf-third.a "$PREFIX/lib/"
echo "MuPDF $MUPDF_VER ($ARCH) installed to $PREFIX"
