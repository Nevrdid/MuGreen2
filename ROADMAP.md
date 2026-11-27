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

User verification:
- **Batch A:** unified data directory, new settings path without legacy migration, canonical document identity, writable-directory validation/fallback reporting, path-only input normalization, and cached document-state directory — verified by the user.
- Document state survives forced termination after the debounce fix.
- Dual-page preference persistence still needs local rebuild/runtime confirmation.
- **Batch B — verified so far:** a normal PDF opens and exits normally; a nonexistent path returns exit code `2`; a text file renamed to `.pdf` returns `4`; normal single-page/dual-page rendering and rotation work; invalid CLI options are recognised; `<datadir>/mugreen2.log` is created and populated.
- Invalid CLI option diagnostics are reported to stderr and are not currently expected in the application log.

**Batch B tests still outstanding (do not mark Batch B fully verified until these are tested):**
- Exit code `3` for an unreadable file — not testable on the user's current test device because it only has a root account. Revisit on a non-root system.
- Exit code `5` for a password-protected PDF — no suitable test document currently available.
- A deliberately broken middle page: confirm the placeholder appears, navigation continues, and the next launch does not get stuck on that page.
- A dual-page spread with one broken page and one valid page.
- Recoverable content/image errors: confirm the warning marker and corresponding log entry.
- Empty PDFs and malformed/non-positive page dimensions, when suitable test files are available.
- A render-failing PDF/page to exercise the stable render-failure exit/recovery path, if a suitable sample becomes available.
- Recheck invalid-path diagnostics in `mugreen2.log` (the user reports the log is populated; this is retained as a distinct check from invalid CLI option handling).

Remaining hardening batches:
- **Batch A — implemented and verified by the user:** unified data directory, new settings path without legacy migration, canonical document identity, writable-directory validation/fallback reporting, path-only input normalization, and cached document-state directory.
- **Batch B — implementation complete; verification partial:** show an on-screen placeholder for page render failures and keep navigation alive; classify document-open failures (missing, unreadable, invalid/unsupported, password-protected, and other backend/I/O failures); provide stable process exit codes and document them in the README. See the outstanding test list above.
- **Batch C — implemented; local verification pending:** `-width` and `-height` now accept only decimal digits in the range `0..65535`, rejecting empty, signed, non-numeric, and out-of-range values before narrowing to `unsigned short`; `0` retains automatic display sizing. README documents `SDL_GAMECONTROLLERCONFIG` (newline-separated entries) and `SDL_GAMECONTROLLERCONFIG_FILE` (mapping database path), including the requirement to set them before launch. The controller detection/banner implementation remains in Phase 3.
- **Batch C tests still outstanding:** build with the target toolchain; verify both dimensions accept `0`, `1`, and `65535`; verify `65536`, negative values, empty values, alphabetic input, and trailing characters are rejected with usage exit code `1`; confirm zero dimensions independently select the display's corresponding dimension (for example, a fixed width with automatic height). Verify a known unmapped controller becomes recognised with a valid custom mapping supplied through each environment variable.

No legacy settings/state migration is planned unless a concrete production requirement appears.

## Corrective rendering fix — continuous dual-page spread

**Status: Implemented on `main`; local build and device verification pending.** This is a focused correction to the existing dual-page view, not the start of Phase 3.

- Treat a facing-page pair as one continuous virtual spread rather than two independently clipped half-screen panes.
- Render only the visible intersection of each page, allowing either page to move naturally beyond the viewport.
- Use the full display width for horizontal spread scrolling and preserve the viewport-centre reading position when zooming. With no pan, that point is the centre of the spread; after panning a page to the centre, zoom remains anchored on that page.
- Keep RTL page ordering and page-pair navigation, and reset/clamp spread offsets when changing layout or rotation.
- When the reader's rotation is 90° or 180°, swap the two physical spread slots to compensate for the corresponding physical handheld turn. 270° remains in the original slot order.
- Add a manual session-only override on Select+D-pad Down (dual-page mode only), leaving Select+B available for night mode for devices whose physical rotation direction differs; it resets the spread pan to a valid origin when toggled.

**User verification:** the continuous spread zoom/scroll issue and the 180° spread-slot compensation are reported fixed. The 90° compensation and manual Select+D-pad Down override are newly implemented and need device testing: rotate the PDF 90° with the reader control, physically turn the handheld 90°, and confirm the first page appears at the physical top; confirm 180° remains correct and 270° remains unchanged. Confirm Select+B still toggles night mode; test Select+D-pad Down in dual-page mode, confirm it reverses the slots and a second press restores them, and confirm it does not affect single-page mode. Also test LTR/RTL, mixed page sizes, first/last single-page cases, dual-page toggling, and restored state.

## Phase 3 — Reading and navigation

**Status: Batch A implemented on `main`; core behaviour and help-panel presentation/rotation verified by the user. TOC rotation and navigation updates are implemented and awaiting device verification. Remaining Phase 3 batches are planned.**

- **Batch A — implemented:** temporary page/spread indicator for 1.2 seconds after the current page changes; default scroll step changed to 0.9 (10% overlap), retaining `-step` overrides; D-pad hold-repeat unified across normal reading, TOC and help, with a 300 ms initial delay and 100 ms repeat interval. The event loop now waits against the next repeat, state-save, or indicator-expiry deadline.
- **User-verified:** normal reading hold-repeat and release behaviour; default scrolling; custom `-step` override; unpaired final page indicator behaviour. The first-pass test reports these working correctly.
- **Presentation corrections — user-verified:** indicator background/readability and page indicator placement/orientation; help panel orientation and rotation-aware scrolling at the tested orientations. Help content includes useful reading guidance and scrolls on shorter logical display heights.
- **TOC rotation — implemented; placement/orientation user-verified:** the panel is drawn at the logical left edge of the overlay and follows the reader's final presentation rotation. It is not pinned to the physical left edge at every orientation. Selection and expand/collapse D-pad directions are mapped to the rotated TOC axes; selection hold-repeat follows the mapped direction.
- **Still to verify:** D-pad selection, expand/collapse and hold-repeat at 0°, 90°, 180° and 270° using a PDF with an outline; TOC state transitions; controller hot-plug cancels repeat cleanly. Build verification has not yet been performed.
- Synchronise the current TOC section with the visible page.
- Fit modes and direct go-to-page workflow.
- Controller presence and mapping feedback:
  - distinguish no physical joystick from an attached joystick without an SDL GameController mapping;
  - show an appropriate on-screen message;
  - support/document mapping configuration for handheld controllers.
- Keep controller input handling consistent across normal reading, TOC, and help overlays, preserving press/release semantics, modifier chords, triggers/axes, and existing bindings.

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
- **Facing-page spreads.** Horizontal stacking is the default; Select+Up toggles vertical stacking and persists. The axis remains stable through reader rotations. RTL and the session-only Select+Down page-order override are supported. A minor occasional previous-spread entry-corner inconsistency was accepted as non-blocking.
- **UI overlays.** TOC, help panel and page indicator passed the user's orientation checks. UI remains upright/readable and is not mirrored with PDF content.
- **Controller mapping and startup configuration.** Select+Left rotates clockwise; Select+Right rotates anti-clockwise. D-pad and face-button actions rotate with the reader; Xbox is the default face-button convention when no saved preference exists, with the selected convention persisted as `xbox_layout=1` (Xbox) or `xbox_layout=0` (Nintendo). `-no-control-rotation` disables reader-relative control mapping. Startup options include `-rotation`, `-display-rotation`, `-rtl` and `-ltr`.
- **Page-only mirroring.** Each PDF page is mirrored independently before it is composited into the logical spread. Mirroring must affect only page pixels: it must not change controller bindings/hotkeys, D-pad scrolling/navigation, spread placement/order, reading direction, reader rotation, or UI overlays. The user tested the final fix and confirmed the behaviour works.
- **Legacy SDL cleanup.** The logical renderer is the only rendering path. The old SDL page renderer, rotation-specific spread geometry, legacy zoom/scroll/page-transition code and temporary overlay compositing helpers were removed. The PDF backend API no longer accepts rotation or mirroring arguments; in-repository rendering uses unrotated pages.
- **Temporary action feedback.** Controller shortcuts display brief labels on the existing transparent UI layer for rotation, reading direction, night mode, mirroring, view reset, spread layout/order and dual-page mode. This is implemented; rapid replacement and expiry while idle still need explicit device verification.
- The user reports overall performance is at least comparable and possibly faster; no formal benchmark has been run.

### Post-merge review backlog

The following items were identified during review of the merged renderer. They are grouped by the kind of work they require; no item below should be treated as fixed until implemented and verified.

#### Correctness and interaction

- **Arrival-corner mapping — implemented; device verification pending.** In single-page mode, `ScrollLogicalPDFPage` now selects the destination corner from the logical D-pad direction rather than `page_delta`: Left/Up enters at the far edge, Right/Down at the near edge. RTL continues to reverse page selection independently, without changing the requested arrival corner. Verify all four reader rotations in both LTR and RTL.
- **D-pad repeat must not auto-chain page turns.** Once a held direction reaches a page edge, the 100 ms repeat can turn successive pages. Require a fresh press to cross the edge, or introduce a deliberate edge-crossing pause, so a held direction cannot rapidly page through the document.
- **Dual-page zoom anchor drift.** In `ZoomLogicalPDFSpread`, convert the anchor pixel to the new scale using the ratio between the new and old scale (capture `old_scale` from the first metrics pass, recompute metrics after changing `finescale`, then multiply `anchor_x` and `anchor_y` by `scale / old_scale`). The single-page zoom path already performs the corresponding page-coordinate conversion.
- **Manual page-order override and RTL must share one navigation rule.** RTL currently swaps slots and flips page delta, while Select+Down swaps slots only. Decide whether the manual override should also reverse D-pad page delta, then use one shared predicate for both. Persist the override in the settings file if it is intended to survive a restart; otherwise document it explicitly as session-only.

#### Rendering performance and resource lifecycle

- **Separate content invalidation from overlay invalidation.** Page-indicator and action-message expiry currently request `FLAG_RENDER`, causing the PDF to be rasterised again (including per-pixel mirror work) merely to update/remove an overlay. Cache the content framebuffer; keep the content render path for document/view changes, and let overlay-only changes redraw the UI layer and call `Present()`.
- **Skip empty overlay uploads.** `Present()` currently uploads the full ARGB overlay texture even when the overlay is empty. Track whether the overlay contains visible content and skip the upload when empty.
- **Prefer an accelerated SDL renderer.** Request `SDL_RENDERER_ACCELERATED` when creating the renderer, retain a fallback if unavailable, and warn when the selected renderer is software. The renderer name is already logged; this is especially relevant to the GE8300 target.
- **Avoid unnecessary framebuffer recreation and handle allocation failure.** Compare the parity of reader rotation plus display rotation rather than the exact angle, because 0°→180° does not change framebuffer dimensions. If framebuffer creation fails, do not leave `g_fb == NULL` and continue into a later resize-handler dereference; return `GREEN_EXIT_RESIZE` or retain the previous valid framebuffer.

#### PDF geometry, caching and diagnostics

- **Cache page bounds independently of display lists.** `GetLogicalSpreadMetrics` can call `pb_device_size` for both pages several times per event. Each call reaches `ensure_page` with a one-entry cache, so alternating pages evict one another. First cache page bounds without retaining display lists; then consider a small 2–3 entry display-list cache.
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

- Automatic page/content crop.
- CBZ and image-document support.
- Configurable cover-page offset for facing-page layouts.
- Night-mode improvements.
- Text search and Unicode text handling.

## Phase 6 — Maintenance and code quality

**Status: Planned.**

- General code organisation after Phase 3 controller-input/navigation interfaces stabilise.
- Consolidate controller help and derive help text from the actual bindings where practical.
- Remove avoidable duplication and clarify module boundaries.
- Keep licensing and third-party notices accurate; update documentation as features land.


## Phase 7 — Presentation polish

**Status: Complete.**

- Minor mandatory presentation fixes completed and committed.
