# MuGreen2 Roadmap

This roadmap tracks implementation scope and verification status. Items are not considered tested merely because their code has been committed; device/build verification is recorded separately.

## Phase 1 — Build-system reliability

**Status: Complete** (native x86_64 and AArch64 builds verified by the user; ARMHF remains untested).

- Compiler/sysroot detection and architecture-specific build configuration.
- MuPDF and SDL2 dependency build scripts, pinned source checksums, and separate install prefixes.
- Dynamic SDL2 linking and runtime compatibility documentation.
- Install targets, `PREFIX`, `DESTDIR`, and `NOSTRIP`.

## Phase 2 — State, persistence, and failure robustness

**Status: In progress — Batch A verified; Batch B partially verified by the user; Batch C implemented and awaiting local verification.**

Already implemented:
- Strict document-state parsing and validation.
- Bounded restored zoom and sanitised scroll offsets.
- Atomic state/settings writes, fsync, and rename.
- Debounced document-state saving, including saving despite a continuously active SDL event queue.
- URI-safe state identity encoding.
- Global night-mode, right-to-left, and dual-page preferences.
- Dual-page preference is saved immediately when toggled.
- Legacy URI migration code and its documentation were removed.
- Unified settings/document-state storage under one resolved data directory.
- Global settings now use `<datadir>/settings`; no legacy `~/.green.settings` migration or fallback is retained.
- Canonicalised existing document paths with `realpath()` before URI encoding, with an absolute-path fallback.
- Require selected data and document-state directories to be writable/searchable; an unusable explicit override is reported before default fallbacks are tried.
- Treat command-line document arguments strictly as filesystem paths and normalize them through one path-to-identity function; cache the document-state directory after first resolution.
- Document the canonical-path identity limitation: moving/renaming a file or opening it through another mount path starts fresh state.
- Zero-page documents are rejected at open time, and empty or non-finite page boxes are rejected or given a safe fallback size.

User verification:
- **Batch A:** unified data directory, new settings path without legacy migration, canonical document identity, writable-directory validation/fallback reporting, path-only input normalization, and cached document-state directory — verified by the user.
- Document state survives forced termination after the debounce fix.
- Dual-page preference persistence still needs local rebuild/runtime confirmation.
- **Batch B — verified so far:** a normal PDF opens and exits normally; a nonexistent path returns exit code `2`; a text file renamed to `.pdf` returns `4`; normal single-page/dual-page rendering and rotation work; invalid CLI options are recognised; `<datadir>/mugreen2.log` is created and populated.
- Invalid CLI option diagnostics are reported to stderr and are not currently expected in the application log.

**Batch B tests still outstanding (do not mark Batch B fully verified until these are tested):**
- Exit code `3` for an unreadable file — not testable as root. Revisit on a non-root system or drop privileges for the test (for example with `setpriv --reuid=1000 ...`).
- Exit code `5` for a password-protected PDF — a sample can be generated with `qpdf --encrypt user owner 256 -- in.pdf out.pdf`.
- A deliberately broken middle page: confirm the placeholder appears, navigation continues past it, and the next launch does not get stuck on that page. A sample can be made by corrupting page 2's content stream after `qpdf --qdf`.
- A dual-page spread with one broken page and one valid page.
- Recoverable content/image errors: confirm the warning marker and corresponding log entry.
- Empty PDFs and malformed/non-positive page dimensions (the code now rejects or falls back; only suitable sample files are missing).
- A render-failing PDF/page to exercise the stable render-failure exit/recovery path, if a suitable sample becomes available.
- Recheck invalid-path diagnostics in `mugreen2.log` (the user reports the log is populated; this is retained as a distinct check from invalid CLI option handling).

Remaining hardening batches:
- **Batch A — implemented and verified by the user:** unified data directory, new settings path without legacy migration, canonical document identity, writable-directory validation/fallback reporting, path-only input normalization, and cached document-state directory.
- **Batch B — implementation complete; verification partial:** show an on-screen placeholder for page render failures and keep navigation alive; classify document-open failures (missing, unreadable, invalid/unsupported, password-protected, and other backend/I/O failures); provide stable process exit codes and document them in the README. See the outstanding test list above.
- **Batch C — implemented; local verification pending:** `-width` and `-height` now accept only decimal digits in the range `0..65535`, rejecting empty, signed, non-numeric, and out-of-range values before narrowing to `unsigned short`; `0` retains automatic display sizing. README documents `SDL_GAMECONTROLLERCONFIG` (newline-separated entries) and `SDL_GAMECONTROLLERCONFIG_FILE` (mapping database path), including the requirement to set them before launch. The controller detection/banner implementation remains in Phase 3.
- **Batch C tests still outstanding:** build with the target toolchain; verify both dimensions accept `0`, `1`, and `65535`; verify `65536`, negative values, empty values, alphabetic input, and trailing characters are rejected with usage exit code `1`; confirm zero dimensions independently select the display's corresponding dimension (for example, a fixed width with automatic height). Verify a known unmapped controller becomes recognised with a valid custom mapping supplied through each environment variable.

- *Optional:* a crash between `mkstemp` and `rename` in the atomic writer leaves `*.tmp.XXXXXX` files behind forever; delete stale ones at startup.

No legacy settings/state migration is planned unless a concrete production requirement appears.

## Corrective rendering fix — continuous dual-page spread

**Status: Implemented on `main`; local build and device verification pending.** This is a focused correction to the existing dual-page view, not the start of Phase 3.

- Treat a facing-page pair as one continuous virtual spread rather than two independently clipped half-screen panes.
- Render only the visible intersection of each page, allowing either page to move naturally beyond the viewport.
- Use the full display width for horizontal spread scrolling and preserve the viewport-centre reading position when zooming. With no pan, that point is the centre of the spread; after panning a page to the centre, zoom remains anchored on that page.
- Keep RTL page ordering and page-pair navigation, and reset/clamp spread offsets when changing layout or rotation.
- Spread geometry is computed in logical (unrotated) coordinates; the complete frame is rotated once at presentation. No rotation-dependent slot swapping exists any more.

**User verification:** the continuous spread zoom/scroll issue is reported fixed. Still to test on the device: zoom in and out several times at each reader rotation in LTR and RTL (the content at the screen centre must stay put, including after panning one page to the centre); turn the handheld to match each reader rotation and confirm the first page is on the reading side at every angle; confirm Select+B still toggles night mode; test Select+D-pad Down in dual-page mode (it must reverse the slots, a second press must restore them, and single-page mode must be unaffected). Also test LTR/RTL, mixed page sizes, first/last single-page cases, dual-page toggling, and restored state.

## Phase 3 — Reading and navigation

**Status: Batch A implemented on `main`; core behaviour and help-panel presentation/rotation verified by the user. TOC rotation and navigation updates are implemented and awaiting device verification. Remaining Phase 3 batches are planned.**

- **Batch A — implemented:** temporary page/spread indicator for 1.2 seconds after the current page changes; default scroll step changed to 0.9 (10% overlap), retaining `-step` overrides; D-pad hold-repeat unified across normal reading, TOC and help, with a 300 ms initial delay and 100 ms repeat interval. The event loop now waits against the next repeat, state-save, or indicator-expiry deadline.
- **User-verified:** normal reading hold-repeat and release behaviour; default scrolling; custom `-step` override; unpaired final page indicator behaviour. The first-pass test reports these working correctly.
- **Presentation corrections — user-verified:** indicator background/readability and page indicator placement/orientation; help panel orientation and rotation-aware scrolling at the tested orientations. Help content includes useful reading guidance and scrolls on shorter logical display heights.
- **TOC rotation — implemented; placement/orientation user-verified:** the panel is drawn at the logical left edge of the overlay and follows the reader's final presentation rotation. It is not pinned to the physical left edge at every orientation. Selection and expand/collapse D-pad directions are mapped to the rotated TOC axes; selection hold-repeat follows the mapped direction.
- **Still to verify:** D-pad selection, expand/collapse and hold-repeat at 0°, 90°, 180° and 270° using a PDF with an outline, both with control rotation enabled and with `-no-control-rotation` (where the D-pad stays physical while the panel is still rotated); TOC state transitions; controller hot-plug cancels repeat cleanly. Build verification has not yet been performed.
- Synchronise the current TOC section with the visible page, and open the TOC on the current section instead of the top. *Current state:* `BuildTOC` resets the selection to 0 and `OpenTOCPanel` never uses `page_cur`.
- Fit modes and direct go-to-page workflow: cycle between width, height and page fit (A currently only resets to width), and add a go-to-page or percentage jump (±10 pages is crude for long books).
- Zoom repeat: X/Y zoom is applied on button release with no hold-to-repeat. Add repeat-while-held (with a short defer or equivalent) while preserving Select-hotkey detection.
- Controller presence and mapping feedback. *Current state:* only a stderr line (`Waiting for an SDL game controller...`) in `Green_SDL_Main`; `controller` is a local variable, so the renderer cannot yet draw a banner. Many handheld pads are plain joysticks that SDL does not map, so `SDL_IsGameController` is false for them.
  - distinguish no physical joystick from an attached joystick without an SDL GameController mapping;
  - show an appropriate on-screen message;
  - support/document mapping configuration for handheld controllers.
- Keep controller input handling consistent across normal reading, TOC, and help overlays, preserving press/release semantics, modifier chords, triggers/axes, and existing bindings.
- *Optional (the project is controller-first, so these are undecided):* a keyboard fallback for desktop development, mapped onto the same logical controls; analog-stick panning.

Previously implemented navigation work to preserve:
- Rotation behaviour across orientations.
- Scrolling and page changes respecting rotation.
- Manga/Japanese right-to-left page ordering and direction.
- Controller navigation and edge-triggered page changes.

Review current `main` before implementing any item; some listed functionality may already be present or partially complete.

## Phase 4 — Rendering and performance

**Status: Logical renderer merged into `main` and core rendering/navigation/mirroring behaviour device-tested by the user. The Phase 4 code/documentation cleanup pass is implemented on `main`; a build after these cleanup edits is still pending. Temporary action-feedback overlay is implemented; its expiry/replacement behaviour still needs explicit device verification. Performance optimisation and the correctness review remain planned.**

### Completed and user-verified

- **Logical framebuffer and final presentation transform.** PDF content is rasterised in unrotated logical page coordinates. The complete content frame is rotated once during SDL presentation. Reader rotation and the independent `-display-rotation=0|90|180|270` physical-mount correction remain separate.
- **Single-page rendering, panning and zoom.** X/Y zoom, viewport-centred anchoring, horizontal/vertical panning, edge clamping and continued zoom after reset or reader rotation were tested by the user.
- **Page and spread navigation.** L1/R1, D-pad edge transitions and rotation-aware horizontal/vertical directions were tested. L2/R2 ten-page skips work in single-page and dual-page modes, including document start/end.
- **Facing-page spreads.** Horizontal stacking is the default; Select+Up toggles vertical stacking and persists. The axis remains stable through reader rotations. Horizontal spread order follows RTL/LTR; vertical spreads retain document order. A minor occasional previous-spread entry-corner inconsistency was accepted as non-blocking.
- **UI overlays.** TOC, help panel and page indicator passed the user's orientation checks. UI remains upright/readable and is not mirrored with PDF content.
- **Controller mapping and startup configuration.** Select+Left rotates clockwise; Select+Right rotates anti-clockwise. D-pad and face-button actions rotate with the reader; Xbox is the default face-button convention when no saved preference exists, with the selected convention persisted as `xbox_layout=1` (Xbox) or `xbox_layout=0` (Nintendo). `-no-control-rotation` disables reader-relative control mapping. Startup options include `-rotation`, `-display-rotation`, `-rtl` and `-ltr`.
- **Legacy SDL cleanup.** The logical renderer is the only rendering path. The old SDL page renderer, rotation-specific spread geometry, legacy zoom/scroll/page-transition code and temporary overlay compositing helpers were removed. The PDF backend API no longer accepts rotation or mirroring arguments; in-repository rendering uses unrotated pages.
- **Temporary action feedback.** Controller shortcuts display brief labels on the existing transparent UI layer for rotation, reading direction, night mode, view reset, spread layout and dual-page mode. This is implemented; rapid replacement and expiry while idle still need explicit device verification.
- **D-pad repeat does not auto-chain page turns.** A physical D-pad press may cross a page edge, but timer-driven repeats only scroll and cannot turn another page. Releasing and pressing again is required for another page turn. Implemented and device-tested.
- The user reports overall performance is at least comparable and possibly faster; no formal benchmark has been run.

### Post-merge review backlog

The following items were identified during review of the merged renderer. They are grouped by the kind of work they require; no item below should be treated as fixed until implemented and verified.

#### Correctness and interaction

- **Page-turn entry — selected-page alignment implemented; device verification pending; entry-point design remains provisional.** Page turns are rotation-independent. In horizontal spreads, the page reached by the turn is selected (the earlier document page when moving forward, the later page when moving backward); the viewport enters at that page's reading-side horizontal edge and aligns its top edge vertically. In vertical spreads, it aligns the selected page's top and left edges. RTL is reflected in horizontal spread order; vertical spreads retain document order. The entire spread remains scrollable: offsets are clamped to the full spread bounds, so a taller or wider partner may initially extend outside the viewport but remains reachable by panning. This replaces the previous spread-outer-boundary alignment, which could place the viewport above a shorter page or beside a narrower page. Test mixed page dimensions in both spread orientations, forward/backward turns, LTR/RTL and all viewport edges. We intend to revisit entry points later so turns can continue from the point where the previous page/spread was exited rather than always using the selected page's leading edge; coordinate this with continuous scrolling, including bottom-entry behaviour. In single-page RTL, the horizontal arrival side should follow the direction of the key pressed rather than the reading-order direction.
- **Dual-page zoom anchor drift — implemented; quick device verification passed.** `ZoomLogicalPDFSpread` captures the old spread scale, recomputes the spread geometry after changing `finescale`, and converts both anchor coordinates by `scale / old_scale` before deriving the new offsets. The user reports that quick tests look correct. The broader matrix (different pan positions, all edges, vertical and horizontal spreads, and mixed page sizes) has not yet been exhaustively checked.
- **Unpaired final page in dual-page mode — implemented; device verification pending.** The final page of an odd-page document has no partner, so spread metrics fall back to the single-page renderer. `LogicalPDFPageScale` now fits it as one slot of a virtual spread (half-width for horizontal spreads, half-height for vertical spreads) and the existing page renderer centres it. The same scale is used for panning, zoom, reading-edge alignment and page skipping; zoom falls back to the single-page anchor calculation when no complete spread exists. Test the final page in horizontal and vertical layouts, all fit modes, zoom/pan, LTR/RTL, and after page skipping. The first page is currently paired with page 2; a separate cover-page offset is planned in Phase 5.
- **RTL/LTR spread ordering — simplified; device verification pending.** Horizontal spreads place the later page on the left in RTL and preserve document order in LTR. Vertical spreads always keep the earlier page above the later page. The manual page-order override and page mirroring have been removed, along with their shortcuts. Verify horizontal and vertical spreads in both reading directions, D-pad progression, shoulder navigation, page-turn entry, and reader rotations.

#### Rendering performance and resource lifecycle

- **Separate content invalidation from overlay invalidation.** Page-indicator and action-message expiry currently request `FLAG_RENDER`, causing the PDF to be rasterised again (including unnecessary page processing) merely to update/remove an overlay. Cache the content framebuffer; keep the content render path for document/view changes, and let overlay-only changes redraw the UI layer and call `Present()`.
- **Skip empty overlay uploads.** `Present()` currently uploads the full ARGB overlay texture even when the overlay is empty. Track whether the overlay contains visible content and skip the upload when empty.
- **Prefer an accelerated SDL renderer.** Request `SDL_RENDERER_ACCELERATED` (and test with vsync) when creating the renderer, retain a fallback if unavailable, and warn when the selected renderer is software. Compare against `SDL_UpdateWindowSurface` on the target. The renderer name is already logged; this is especially relevant to the GE8300 target.
- **Strip/tile rendering.** Rasterise a strip or tile larger than the viewport (for example about 1.5× the screen height) and make a scroll step a blit instead of a full-screen re-rasterisation. Relevant on a Cortex-A53; pairs with the continuous-scrolling prerequisites in Phase 5.
- **Avoid unnecessary framebuffer recreation and handle allocation failure.** Compare the parity of reader rotation plus display rotation rather than the exact angle, because 0°→180° does not change framebuffer dimensions. If framebuffer creation fails, do not leave `g_fb == NULL` and continue into a later resize-handler dereference; return `GREEN_EXIT_RESIZE` or retain the previous valid framebuffer.
- **Resolve TOC page destinations lazily.** `pb_outline_load` resolves the page of every outline item up front, which is slow for large outlines. Store the URI and resolve it when the entry is selected.
- **Pre-render the bitmap font into a glyph atlas.** Lower priority now that overlays are drawn only on change, but a full TOC redraw is still thousands of `SDL_FillRect` calls.

#### PDF geometry, caching and diagnostics

- **Cache page bounds independently of display lists.** `GetLogicalSpreadMetrics` can call `pb_device_size` for both pages several times per event. Each call reaches `ensure_page` with a one-entry cache, so alternating pages evict one another. First cache page bounds without retaining display lists; then consider a small 2–3 entry display-list cache.
- **Profile page-turn latency** with different MuPDF store sizes and consider a larger default for dual-page manga reading; expose anti-aliasing as an `-aa=` option.
- **Make render-failure diagnostics actionable and bounded.** The placeholder exists, but failure text currently goes only to stderr, can repeat on every redraw, and the README claims the message is also logged. Log once per page; add timestamps and a size cap/rotation policy to the application log, and align the README with actual behaviour.
- **Accept documented decimal scroll steps.** `-step=0.9` is documented as the default, but `ReadFraction` currently rejects decimal notation, so users can only enter an equivalent fraction such as `9/10`. Either accept decimals or change the documented syntax/default representation.

#### Code and documentation cleanup

**Implemented on `main`; build verification after these edits is pending.**

- Removed the obsolete rotation-aware legacy scrolling/zoom helpers and their declarations: `Green_ScrollRelative`, `Green_Zoom`, `Green_GetScrollRegion`, `Green_ValidateOffset` and `Green_GetDimension`. Removed the unused `bb` fields and their initialization/transfer.
- Removed unused `g_overlay_rotation`, the duplicate `DrawString` prototype, stale renderer comments and excessive blank space.
- Simplified `RenderTOCContent` to draw at the logical left edge; the final presentation transform rotates the TOC with the reader. Updated the code comment, README and Phase 3 TOC description to match.
- Added `Green_FitLogical` and replaced the repeated temporary-document rotation resets in the SDL renderer.
- Updated `examples/launch-mugreen2.sh` to pass `-display-rotation`, configurable through `MUGREEN2_DISPLAY_ROTATION`; the obsolete `GREEN_ROTATE180` variable is no longer used.
- Documented the actual startup-override persistence: `-rotation` is saved in per-document state, while `-rtl`/`-ltr` are saved in global settings on exit.
- Corrected both README controller-layout descriptions to make Xbox the default, and added a physical-position-to-action table for Nintendo and Xbox layouts.
- Removed the obsolete `EXPERIMENT_RENDER_PIPELINE.md`; merged architecture and validation notes are retained in this roadmap.

## Phase 5 — Reader features

**Status: Planned.**

- **Continuous scrolling mode:** support both vertical and horizontal scrolling in single-page and dual-page layouts. Revisit page-turn entry from the bottom as part of this work. *Prerequisites from Phase 4:* cached page bounds, a display-list cache, strip/tile rendering, and a content framebuffer that survives overlay-only changes; without these, scrolling will be sluggish on a Cortex-A53.
- Automatic page/content crop: run the display list through `fz_new_bbox_device` in the backend's page setup and shrink the cached page bounds, so everything downstream (fit, scroll limits, spreads) uses the cropped box. Use a uniform per-document crop (for example the union over sampled pages) to avoid jumpy layout between pages. This is likely the largest readability gain on a small screen.
- CBZ and image-document support (both are currently disabled in `build-mupdf.sh`). They need no extra fonts, so the binary-size cost is small; EPUB would need fonts and could stay behind a build flag.
- Configurable cover-page offset for facing-page layouts (pairs are currently hard-wired as even/odd). Persist dual-page mode (and spread orientation) per document instead of only globally.
- Night-mode improvements: the current mode is a plain pixel inversion, which also inverts photographs. Investigate a luminance-preserving variant in MuPDF 1.24 (verify what the API provides), and optionally brightness/contrast or sepia.
- Text search: `pb_search` exists in the backend but is unused. It needs a controller-driven input method (for example a letter picker) and hit highlighting.
- Unicode text handling: the bitmap font is ASCII only, so non-ASCII shows as `?` (accented TOC entries lose their accents). A cheap interim step is transliteration in `DrawUtf8`; the proper fix is `stb_truetype` with a small embedded font (or SDL2_ttf).
- *Ideas under consideration (unscheduled):* bookmarks and a recent-files list, following internal links, and a built-in file picker for use without a frontend.

## Phase 6 — Maintenance and code quality

**Status: Planned.**

- General code organisation after Phase 3 controller-input/navigation interfaces stabilise.
- Consolidate controller help and derive help text from the actual bindings where practical.
- Remove avoidable duplication and clarify module boundaries. Splitting `sdl.c` (input, TOC, text, render) fits here; one action/binding table could generate both the help screen and the README control table. Remove the stale `"Optional / unbound (NULL)"` string comparison in `RenderHelpContent`, whose line no longer exists.
- Keep licensing and third-party notices accurate; update documentation as features land.

## Phase 7 — Presentation polish

**Status: Complete.**

- Minor mandatory presentation fixes completed and committed.

Optional follow-ups (not blocking):
- Fix the stray `-controller-layout` row above the Input table in the README, which renders as a broken table.
- Add screenshots to the README.
- Add a CI cross-build that publishes binaries.