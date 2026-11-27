# MuGreen2

A lightweight, controller-driven PDF reader based on the Green reader, using **MuPDF** for PDF rendering and **SDL2** for display and game-controller input through SDL's GameController API.

This is a single-document reader for embedded Linux devices. The build supports AArch64 and 32-bit ARM hard-float targets.

## Features

- Opens one PDF document per invocation.
- MuPDF-backed PDF rendering.
- SDL2 display and controller input.
- Document table of contents (when present in the PDF).
- Controller-oriented navigation, zoom, rotation, mirroring, dual-page view, and night mode.
- Remembers reader state between launches.
- Optional right-to-left page navigation for manga and other right-to-left documents.

### Controller layout

The on-screen help panel lists the current bindings. The default Nintendo-labelled layout is:

| Input | Action |
| --- | --- |
| D-pad | Scroll / navigate the table of contents |
| B | Fit page to width / select TOC entry |
| A | Rotate right / close overlay |
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

Some controls are optional and depend on the controller exposing the corresponding SDL input.

## Differences from the original Green

MuGreen2 is a focused adaptation of Florian Tobias Schandinat's [Green](https://github.com/schandinat/green), not a from-scratch replacement. It keeps the Green reader's SDL-based application structure and configuration concepts while changing the PDF engine and tailoring the reading workflow.

| Area | Original Green | MuGreen2 |
| --- | --- | --- |
| PDF engine | libpoppler | MuPDF, through the dedicated `pdf_backend` wrapper |
| Documents | Multiple documents could be opened and switched between in one process | One PDF document is opened per invocation |
| Input | SDL frontends with keyboard-oriented navigation | SDL2 GameController input with controller hot-plug support |
| Navigation | Page scrolling, page stepping, direct page entry and text search | Page scrolling and shoulder-button page stepping, plus navigation through the PDF's document outline / table of contents |
| Display | Single-page viewing, fit modes and zoom | Fit and zoom, rotation, horizontal/vertical mirroring, optional dual-page view and night mode |
| Reading state | Configuration and display schemes | Per-document reading position and view state are saved, alongside the user's night-mode setting |

### What this means in practice

- MuPDF replaces libpoppler as the PDF parsing and rendering engine.
- The reader is deliberately single-document: launch `mugreen2` again for another PDF rather than switching between several open documents.
- Input is handled through SDL2's GameController API. MuGreen2 currently uses a controller-first interface; keyboard navigation is not implemented.
- The PDF outline is exposed as an on-screen, navigable table of contents when the document provides one.
- The view can be rotated or mirrored, shown in dual-page mode, and switched to night mode. Per-document position and view settings are restored on a later launch.
- Green's multi-document switching, interactive text-search workflow and direct page-entry workflow are not currently exposed in MuGreen2. The original configuration/scheme mechanism remains available.

The original Green-derived files retain Florian Tobias Schandinat's copyright and GPL-3.0 notices. MuPDF is integrated as the rendering backend; see the licensing notes below and the `LICENSE` file for the terms applying to the combined application.

## Build

The build scripts accept `ARCH` and `CROSS_COMPILE`. Install the matching cross-toolchain and SDL2 development files first.

1. Build the trimmed static MuPDF libraries:

   ```sh
   ARCH=aarch64 CROSS_COMPILE=/path/to/bin/aarch64-linux-gnu- PREFIX=/opt/mupdf-aarch64 ./build-mupdf.sh
   ```

   For 32-bit hard-float ARM, use `ARCH=armhf` with a suitable hard-float compiler prefix, such as `arm-linux-gnueabihf-`.

2. Build MuGreen2:

   ```sh
   make ARCH=aarch64 CROSS_COMPILE=/path/to/bin/aarch64-linux-gnu- SYSROOT=/path/to/sysroot MUPDF=/opt/mupdf-aarch64 SDL2_CONFIG=/path/to/sdl2-config
   ```

   Keep `ARCH`, `CROSS_COMPILE`, `SYSROOT`, and `MUPDF` consistent between the two builds.

3. Run it with one PDF:

   ```sh
   ./mugreen2 /path/to/document.pdf
   ```

The MuPDF build script downloads the MuPDF 1.24.11 source release and builds the static libraries. It requires network access for that download on the first run.

## Saved settings

The night-mode and right-to-left reading preferences remain global and are stored in `~/.green.settings`. Per-document reading state is stored separately under `$XDG_DATA_HOME/mugreen2/documents/` (or `~/.local/share/mugreen2/documents/` when `XDG_DATA_HOME` is unset). Each PDF gets a compact state filename derived from its URI; the URI is also recorded inside the state file to guard against hash collisions.

## Licensing

MuGreen2 is distributed under the **GNU Affero General Public License, version 3 only (AGPL-3.0-only)**. See [LICENSE](LICENSE).

The project is derived from Green, which originally carried its own GPL-3.0 notices. Those original copyright and license notices are retained in the source files. MuPDF is also available under the AGPL; see [MuPDF](https://mupdf.com/) and the upstream [Artifex MuPDF repository](https://github.com/ArtifexSoftware/mupdf).

MuPDF's store defaults to 32 MiB. Override it per launch with `-cache=MB`, for example `mugreen2 -cache=64 manual.pdf` (accepted range: 1–1024 MiB).

See [THIRD_PARTY.md](THIRD_PARTY.md) for third-party component and licensing details.

## Acknowledgements

See [THANKS.md](THANKS.md) for the upstream projects and contributors.
