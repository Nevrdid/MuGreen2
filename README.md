# MuGreen2

A lightweight, controller-driven PDF reader based on the Green reader, using **MuPDF** for PDF rendering and **SDL2** for display and game-controller input.

This is a single-document reader intended for embedded Linux devices, with an AArch64 / Cortex-A53 cross-build configuration.

## Features

- Opens one PDF document per invocation.
- MuPDF-backed PDF rendering.
- SDL2 display and controller input.
- Document table of contents (when present in the PDF).
- Controller-oriented navigation, zoom, rotation, mirroring, dual-page view, and night mode.
- Remembers reader state between launches.

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

Some controls are optional and depend on the controller exposing the corresponding SDL input.

## Differences from the original Green

MuGreen2 is a focused adaptation of Florian Tobias Schandinat's [Green](https://github.com/schandinat/green), not a from-scratch replacement. It keeps the Green reader's SDL-based application structure and configuration concepts while changing the PDF engine and tailoring the reading workflow.

| Area | Original Green | MuGreen2 |
| --- | --- | --- |
| PDF engine | libpoppler | MuPDF, through the dedicated `pdf_backend` wrapper |
| Documents | Multiple documents could be opened and switched between in one process | One PDF document is opened per invocation |
| Input | SDL frontends with keyboard-oriented navigation | SDL2 keyboard input plus an integrated SDL GameController mapping |
| Navigation | Page scrolling, page stepping, direct page entry and text search | Page scrolling and shoulder-button page stepping, plus navigation through the PDF's document outline / table of contents |
| Display | Single-page viewing, fit modes and zoom | Fit and zoom, rotation, horizontal/vertical mirroring, optional dual-page view and night mode |
| Reading state | Configuration and display schemes | Per-document reading position and view state are saved, alongside the user's night-mode setting |

### What this means in practice

- MuPDF replaces libpoppler as the PDF parsing and rendering engine.
- The reader is deliberately single-document: launch `mugreen2` again for another PDF rather than switching between several open documents.
- A controller can operate the reader without relying on a keyboard. Keyboard input is still supported by the SDL interface.
- The PDF outline is exposed as an on-screen, navigable table of contents when the document provides one.
- The view can be rotated or mirrored, shown in dual-page mode, and switched to night mode. Per-document position and view settings are restored on a later launch.
- Green's multi-document switching, interactive text-search workflow and direct page-entry workflow are not currently exposed in MuGreen2. The original configuration/scheme mechanism remains available.

The original Green-derived files retain Florian Tobias Schandinat's copyright and GPL-3.0 notices. MuPDF is integrated as the rendering backend; see the licensing notes below and the `LICENSE` file for the terms applying to the combined application.

## Build

The supplied build scripts are configured for an AArch64 Cortex-A53 target and expect the ARM GNU toolchain and SDL2 development files at the paths shown in `Makefile` and `build-mupdf.sh`.

1. Install the AArch64 cross toolchain and SDL2 2.30.8 (or adjust the paths in the build files).
2. Build the trimmed static MuPDF libraries:

   ```sh
   PREFIX=/opt/mupdf-aarch64 ./build-mupdf.sh
   ```

3. Build MuGreen2:

   ```sh
   make MUPDF=/opt/mupdf-aarch64
   ```

4. Run it with one PDF:

   ```sh
   ./mugreen2 /path/to/document.pdf
   ```

The MuPDF build script downloads the MuPDF 1.24.11 source release and builds the static libraries. It requires network access for that download on the first run.

## Saved settings

The night-mode preference is stored in `~/.green.settings`. The reader's per-document navigation state is stored alongside the PDF using the Green state-file mechanism.

## Licensing

MuGreen2 is distributed under the **GNU Affero General Public License, version 3 only (AGPL-3.0-only)**. See [LICENSE](LICENSE).

The project is derived from Green, which originally carried its own GPL-3.0 notices. Those original copyright and license notices are retained in the source files. MuPDF is also available under the AGPL; see [MuPDF](https://mupdf.com/) and the upstream [Artifex MuPDF repository](https://github.com/ArtifexSoftware/mupdf).

## Acknowledgements

See [THANKS.md](THANKS.md) for the upstream projects and contributors.
