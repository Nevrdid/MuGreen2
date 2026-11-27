# MuGreen2

A lightweight, controller-driven PDF reader based on the Green reader, using **MuPDF** for PDF rendering and **SDL2** for display and game-controller input through SDL's GameController API.

This is a single-document reader for Linux devices. The build supports AArch64, 32-bit ARM hard-float, and native host targets.

## Features

- Opens one PDF document per invocation.
- MuPDF-backed PDF rendering.
- SDL2 display and controller input.
- Document table of contents (when present in the PDF).
- Controller-oriented navigation, zoom, rotation, mirroring, dual-page view, and night mode.
- Remembers reader state between launches.
- Optional right-to-left page navigation for manga and other right-to-left documents.

### Controller layout

| `-controller-layout=nintendo\|xbox` | Select Nintendo-labelled or Xbox (default) face-button layout; Nintendo swaps A/B and X/Y |

| Input | Action |
| --- | --- |
| D-pad | Scroll / navigate the table of contents |
| A | Fit page to width / select TOC entry |
| Select + Left | Rotate clockwise |
| Select + Right | Rotate anti-clockwise |
| X / Y | Zoom in / zoom out |
| L1 / R1 | Previous / next page |
| L2 / R2 | Jump -10 / +10 pages (optional) |
| Start | Toggle document table of contents |
| Select | Tap for help; hold as a modifier |
| Select + Start | Exit |
| Guide | Exit (optional) |
| Select + B | Toggle night mode |
| Select + X / Y | Mirror horizontally / vertically |
| Select + L1 | Toggle dual-page mode |
| Select + R1 | Toggle right-to-left manga reading |

Holding a D-pad direction repeats navigation after a 300 ms initial delay, then every 100 ms. Normal reading scrolls by 90% of the viewport by default, leaving a 10% overlap; use `-step=<fraction>` to override this. After the visible page or spread changes, a small page-number overlay appears for about 1.2 seconds. The page indicator and help panel follow the PDF's reader rotation.

Some controls are optional and depend on the controller exposing the corresponding SDL input.

The selected layout determines which physical button position receives each action:

| Physical position | Nintendo layout | Xbox layout (default) |
| --- | --- | --- |
| East | A — fit page / select TOC; Select+A has no extra action | B — Select+B toggles night mode |
| South | B — Select+B toggles night mode | A — fit page / select TOC; Select+A has no extra action |
| North | X — zoom in; Select+X mirrors horizontally | Y — zoom out; Select+Y mirrors vertically |
| West | Y — zoom out; Select+Y mirrors vertically | X — zoom in; Select+X mirrors horizontally |


### Custom SDL2 controller mappings

MuGreen2 uses SDL2's GameController API. If SDL2 does not recognise a controller, or its default button/axis layout is unsuitable, you can provide additional SDL GameController database mappings through either of these environment variables:

- `SDL_GAMECONTROLLERCONFIG`: one or more mapping entries, separated by newlines.
- `SDL_GAMECONTROLLERCONFIG_FILE`: path to a text file containing mapping entries, one per line.

Set the variable in the environment **before launching MuGreen2**, because SDL reads these hints during `SDL_Init(SDL_INIT_GAMECONTROLLER)`.

For example, to load a mapping database from a file:

```sh
SDL_GAMECONTROLLERCONFIG_FILE=/path/to/gamecontrollerdb.txt ./mugreen2 /path/to/document.pdf
```

A mapping entry follows SDL's GameController database format: controller GUID, controller name, then comma-separated logical button/axis bindings (for example, `a:b0` or `leftx:a0`). Use a mapping generated for the exact controller and connection mode; GUIDs and physical input indices can differ between USB and Bluetooth modes. The SDL mapping database and SDL's `controllermap` utility are useful references when creating entries.

These variables supply mappings to SDL; they do not change MuGreen2's own button assignments. The reader still requires a device recognised by SDL as a GameController. Controller-presence and missing-mapping on-screen feedback are tracked separately in Phase 3.

## Differences from the original Green

MuGreen2 is a focused adaptation of Florian Tobias Schandinat's [Green](https://github.com/schandinat/green), not a from-scratch replacement. It keeps the Green reader's SDL-based application structure while changing the PDF engine and tailoring the reading workflow.

| Area | Original Green | MuGreen2 |
| --- | --- | --- |
| PDF engine | libpoppler | MuPDF, through the dedicated `pdf_backend` wrapper |
| Documents | Multiple documents could be opened and switched between in one process | One PDF document is opened per invocation |
| Input | SDL frontends with keyboard-oriented navigation | SDL2 GameController input with controller hot-plug support |
| Navigation | Page scrolling, page stepping, direct page entry and text search | Page scrolling and shoulder-button page stepping, plus navigation through the PDF's document outline / table of contents |
| Display | Single-page viewing, fit modes and zoom | Fit and zoom, rotation, horizontal/vertical mirroring, optional dual-page view and night mode |
| Reading state | Configuration and display schemes | Per-document reading position and view state are saved, alongside global night-mode and reading-direction preferences |

### What this means in practice

- MuPDF replaces libpoppler as the PDF parsing and rendering engine.
- The reader is deliberately single-document: launch `mugreen2` again for another PDF rather than switching between several open documents.
- Input is handled through SDL2's GameController API. MuGreen2 currently uses a controller-first interface; keyboard navigation is not implemented.
- The PDF outline is exposed as an on-screen, navigable table of contents when the document provides one.
- The view can be rotated or mirrored, shown in dual-page mode, and switched to night mode. In dual-page mode, both pages form one continuous spread for scrolling and zooming; zoom is anchored at the current viewport centre. Horizontal stacking is the default and remains horizontal from the user's viewing orientation at all reader rotations; hold Select and press D-pad Up to toggle vertical stacking. Hold Select and press D-pad Down to manually reverse facing-page order when needed. Select+B remains the night-mode shortcut. The stack-axis preference is persisted; the manual page-order override lasts for the current reader session. Per-document position and view settings are restored on a later launch.
- Green's multi-document switching, interactive text-search workflow, direct page-entry workflow and legacy configuration/scheme system are not implemented in MuGreen2. Window size, fullscreen mode and MuPDF cache size can be set with command-line options.

The original Green-derived files retain Florian Tobias Schandinat's copyright and GPL-3.0 notices. MuPDF is integrated as the rendering backend; see the licensing notes below and the `LICENSE` file for the terms applying to the combined application.

## Build

The build scripts support `ARCH=aarch64`, `ARCH=armhf`, and `ARCH=native`. Install the matching compiler and, for cross-builds, a target sysroot containing the platform development files required by SDL2. AArch64/ARMHF builds require libdrm, libgbm, EGL and GLES2 headers and libraries for KMSDRM/OpenGL ES 2. If `SYSROOT` is not supplied, the build scripts ask the selected compiler for its sysroot with `-print-sysroot`; a compiler with no separate sysroot is supported.

1. Build SDL2 for the target architecture:

   ```sh
   ARCH=aarch64 ./build-sdl2.sh
   ```

   SDL2 is built as a shared library and installed under `sdl2/aarch64/`, `sdl2/armhf/`, or `sdl2/native/`, keeping each architecture separate. The Makefile uses `sdl2-config --libs` and dynamically links against `libSDL2-2.0.so.0`. The target must provide a compatible SDL2 runtime library. For a native build, use `ARCH=native ./build-sdl2.sh`; this explicitly ignores an inherited `CROSS_COMPILE`. The script builds from the SDL2 2.30.8 release by default; set `SDL2_VER` to select another release. The default release has a pinned SHA-256; other versions require an explicit `SDL2_SHA256`. Cross-builds fail if KMSDRM, EGL or GLES2 support is not detected.

2. Build the trimmed static MuPDF libraries:

   ```sh
   ARCH=aarch64 ./build-mupdf.sh
   ```

   For 32-bit hard-float ARM, use `ARCH=armhf` with a suitable hard-float compiler prefix, such as `arm-linux-gnueabihf-`. The script selects an ARMHF compiler prefix by default for this target. For a native build, run `ARCH=native ./build-mupdf.sh`; it uses the host compiler without a sysroot.

   By default, the installed headers and libraries are kept separately in `mupdf/aarch64/`, `mupdf/armhf/`, and `mupdf/native/`. Building one architecture therefore won't overwrite another architecture's installed MuPDF files. Set `MUPDF_PREFIX` to override the MuPDF install location. `PREFIX` is reserved for the application install target in the Makefile.

3. Build MuGreen2 against the matching MuPDF and SDL2 development files:

   ```sh
   make ARCH=aarch64 MUPDF=mupdf/aarch64 SDL2_CONFIG="$PWD/sdl2/aarch64/bin/sdl2-config"
   ```

   For native builds, use `make ARCH=native MUPDF=mupdf/native SDL2_CONFIG="$PWD/sdl2/native/bin/sdl2-config"`. The Makefile forces the host compiler for `ARCH=native`. Keep `ARCH`, `CROSS_COMPILE`, and `SYSROOT` consistent between cross-builds. The compiler's reported sysroot is used automatically unless `SYSROOT` is explicitly set. The Makefile selects `mupdf/$(ARCH)` automatically; you can override this with `MUPDF=/path/to/mupdf`.

**Runtime compatibility matters:** SDL2 2.x uses the `libSDL2-2.0.so.0` SONAME, but that alone does not guarantee that an application built against a newer SDL2 can run with an older one; newer SDL APIs may be missing from the older runtime. For each handheld target, build MuGreen2 against the SDL2 headers and library version that the target actually provides (from its sysroot / SDK), and point `SDL2_CONFIG` at that matching `sdl2-config`. Alternatively, if you use the SDL2 built by `build-sdl2.sh`, deploy that exact shared SDL2 runtime alongside the application or install it on the device. Do not assume the device's preinstalled SDL2 is interchangeable without checking its version. The SDL2 build script defaults to 2.30.8; set `SDL2_VER` to match the target's SDL2 release when building a replacement.

4. Install the executable (optional):

   ```sh
   make install PREFIX=/usr/local
   ```

   Set `DESTDIR` to stage installation into a package root. To keep debug symbols, use `make NOSTRIP=1`; the default build strips the executable.

5. Run it with one PDF:

   ```sh
   ./mugreen2 /path/to/document.pdf
   ```

The build embeds `git describe --tags --always --dirty` in the executable. When building from a source archive without Git metadata, the version is reported as `unknown`; override it with `make VERSION=...`.

## Command-line options

Exactly one PDF path is required. Options may be placed before or after the path:

| Option | Description |
| --- | --- |
| `-fit=none\|width\|height\|page` | Initial fit mode (default: `width`) |
| `-step=<fraction>` | Scroll step as a fraction of the viewport (default: `0.9`, leaving 10% overlap) |
| `-zoomstep=<fraction>` | Zoom increment above 1 (default: `1/5`, i.e. 20%) |
| `-controller-layout=nintendo\|xbox` | Select Nintendo-labelled or Xbox (default) face-button layout; Nintendo swaps A/B and X/Y |
| `-no-control-rotation` | Keep controller directions and face-button actions fixed when the reader rotates |
| `-fullscreen` | Start fullscreen |
| `-no-fullscreen` | Start windowed |
| `-width=<pixels>` | Initial window width (`0`–`65535`); `0` uses the display width |
| `-height=<pixels>` | Initial window height (`0`–`65535`); `0` uses the display height |
| `-cache=<MB>` | MuPDF store size in MiB (1–1024; default: `32`) |
| `-help` / `-h` | Show command-line help |
| `-version` / `-v` | Show version, license and project URL |

For example: `mugreen2 -fullscreen -fit=page -cache=64 manual.pdf`.

### Exit codes

MuGreen2 uses stable non-zero exit codes so launchers can distinguish common failures:

| Code | Meaning |
| ---: | --- |
| 0 | Success / normal exit |
| 1 | Invalid command-line options or document argument count |
| 2 | PDF file not found |
| 3 | PDF file is not readable |
| 4 | Not a regular file, or the file is not a supported/valid document |
| 5 | PDF requires a password |
| 6 | Other document-opening failure (I/O, memory, or MuPDF initialisation) |
| 7 | SDL initialisation failure |
| 8 | SDL window, renderer, or initial framebuffer setup failure |
| 9 | Initial PDF page rendering failure |
| 10 | SDL event-wait failure |
| 11 | Framebuffer resize failure |

Document-opening failures print a diagnostic to standard error and append a message to `<datadir>/mugreen2.log` when the application data directory is writable. A page that fails after the reader has displayed a valid frame is replaced with an on-screen error placeholder; navigation remains available. Render failures and recoverable content warnings are also appended to the same log file. If the first frame cannot be rendered, the reader exits with code 9 and advances the saved position past the failed page where possible.

## Startup orientation

Use `-rotation=0|90|180|270` to override the saved reader rotation for this launch.
Use `-display-rotation=0|90|180|270` to rotate the complete presented frame for
the physical display mounting; this is independent of PDF rotation and controller
navigation. Use `-rtl` or `-ltr` to override the saved initial reading direction.

A sample shell launcher is provided at `examples/launch-mugreen2.sh`.

Both dependency build scripts verify pinned SHA-256 checksums before extracting their default source releases: MuPDF 1.24.11 and SDL2 2.30.8. Other MuPDF versions require an explicit `MUPDF_SHA256` value. Network access is required for the first download. Checksum verification is performed when the archive is extracted; if the corresponding source directory already exists, the script reuses it without re-verifying the extracted tree. Remove that directory to force a fresh, verified extraction.

## Saved settings and document state

Global preferences (night mode, right-to-left reading, dual-page mode, spread orientation, and controller face-button layout) are stored in `<datadir>/settings`. Per-document reading state is stored in `<datadir>/documents/`.

The data directory is selected in this order:

1. `MUGREEN2_DATA_DIR`, when set (relative paths are resolved against the current working directory).
2. `$XDG_DATA_HOME/mugreen2`, when `XDG_DATA_HOME` is an absolute path.
3. `$HOME/.local/share/mugreen2`, when `HOME` is an absolute path.
4. `.mugreen2-data/` beside the running executable, as a last resort.

The selected directory is created with user-only permissions where possible. If no writable location is available, MuGreen2 prints one warning to stderr and continues without saving settings or document state. For launchers and read-only system installations, set `MUGREEN2_DATA_DIR` to a writable per-user or per-device directory.

Document paths are canonicalised with `realpath()` before being converted to file URIs, so equivalent paths (including paths containing `.` or `..`) share the same state. If canonicalisation fails, an absolute-path fallback is used. URI-reserved characters, percent signs, spaces, and non-ASCII bytes are percent-encoded. Each document state file stores its URI to guard against hash collisions. Document identity is based on the canonical filesystem path: renaming or moving a PDF, or accessing the same file through a different mount point, starts with fresh state.

State files are strictly parsed and invalid values are ignored; restored zoom is bounded to a fine-scale range of 0.05–20.0, and negative scroll offsets are reset before the display bounds are applied. State writes use a private temporary file, fsync, and atomic rename. While reading, changes are saved after a 750 ms idle period, as well as when the document closes or the application exits. Global settings use the same atomic-write procedure.

## Licensing

MuGreen2 is distributed under the **GNU Affero General Public License, version 3 only (AGPL-3.0-only)**. See [LICENSE](LICENSE). The Green-derived portions retain their original GPL-3.0 notices and copyright attribution; MuGreen2 modifications are identified in the relevant source headers.

The project is derived from Green, which originally carried its own GPL-3.0 notices. Those original copyright and license notices are retained in the source files. MuPDF is also available under the AGPL; see [MuPDF](https://mupdf.com/) and the upstream [Artifex MuPDF repository](https://github.com/ArtifexSoftware/mupdf).

See [THIRD_PARTY.md](THIRD_PARTY.md) for third-party component and licensing details.

## Acknowledgements

See [THANKS.md](THANKS.md) for the upstream projects and contributors.
