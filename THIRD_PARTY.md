# Third-party components

## MuPDF 1.24.11

- Project: https://mupdf.com/
- Source: https://github.com/ArtifexSoftware/mupdf
- License: GNU Affero General Public License version 3 (AGPL-3.0), or commercial licensing from Artifex.
- `build-mupdf.sh` downloads the 1.24.11 source release, verifies its pinned SHA-256 checksum, and builds static libraries.
- The install location is controlled by `MUPDF_PREFIX` (default: `mupdf/<architecture>/`).

## SDL2

- Project: https://www.libsdl.org/
- Source: https://github.com/libsdl-org/SDL
- License: zlib License.
- `build-sdl2.sh` downloads the SDL2 2.30.8 source release and verifies its pinned SHA-256 checksum before extraction.
- SDL2 is built as a static library (`--disable-shared --enable-static`) and installed under `sdl2/<architecture>/` by default. The install location is controlled by `SDL2_PREFIX`.
- AArch64 and ARMHF builds explicitly enable KMSDRM and EGL/OpenGL ES 2, and disable X11, Wayland, Vulkan and audio backends. They require the matching target sysroot to provide DRM, GBM, EGL and GLES development headers and libraries. These platform libraries are not bundled into SDL2 or the application; they must be present on the target device.
- The native build retains SDL2 host-detected video backends for desktop development.

Consult the upstream projects for their complete license texts and notices.
