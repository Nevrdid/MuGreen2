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
- **TOC rotation — implemented, awaiting device verification:** the TOC is composited in reader-rotated coordinates, with its panel kept on the physical left edge in all four orientations. Selection and expand/collapse D-pad directions are mapped to the rotated TOC axes; selection hold-repeat follows the mapped physical direction.
- **Still to verify:** TOC panel placement/orientation and D-pad selection plus expand/collapse at 0°, 90°, 180° and 270° using a PDF with an outline; TOC hold-repeat and state transitions; controller hot-plug cancels repeat cleanly. Build verification has not yet been performed.
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

**Status: Logical renderer cleanup built and device-tested by the user. Temporary action feedback is implemented and awaiting device verification. Not merged into `main`.**

- **Logical framebuffer and final presentation transform — implemented and device-tested.** PDF content is rasterised without document rotation into a logical framebuffer; the complete content frame is rotated once during SDL presentation. Reader rotation and the independent `-display-rotation=0|90|180|270` mounting correction are supported.
- **Single-page rendering, panning and zoom — implemented and device-tested.** The user confirmed X/Y zoom, viewport-centred anchoring, horizontal/vertical panning, edge clamping and continued zoom operation after reset or reader rotation.
- **Page and spread navigation — implemented and device-tested.** L1/R1, D-pad edge transitions and rotation-aware horizontal/vertical direction mappings work in the user's tests. L2/R2 ten-page skips were confirmed in single-page and dual-page modes, including document start/end.
- **Facing-page spreads — implemented and device-tested.** Horizontal stacking is the default; Select+Up toggles vertical stacking and the choice persists. The axis remains stable through reader rotations. RTL and the explicit Select+Down page-order override are supported. A minor occasional previous-spread entry-corner inconsistency remains accepted as non-blocking.
- **UI overlays — implemented and device-tested.** TOC placement, help panel and page indicator passed the user's orientation checks. UI stays readable and is not mirrored with PDF content.
- **Controller and startup configuration — implemented and device-tested.** Select+Left rotates clockwise; Select+Right rotates anti-clockwise. Startup options include `-rotation=0|90|180|270`, `-display-rotation=0|90|180|270`, and `-rtl` / `-ltr`.
- **Legacy SDL cleanup — implemented, built and device-tested.** The logical renderer is now the only rendering path. The old SDL page renderer, rotation-specific spread geometry, legacy zoom/scroll/page-transition code and temporary overlay-surface compositing helpers were removed. The `sdl.c` file decreased from 2,768 to 2,272 lines in this cleanup pass. The PDF backend API no longer accepts rotation or mirroring arguments; all in-repository call sites render unrotated pages. A pre-cleanup safety branch, `experiment-render-pipeline-pre-cleanup`, preserves the previously tested parallel implementation.
- **Rotation-aware controller mapping and mirror/orientation separation — implementation updated on this branch; build and device verification pending.** Button events are normalised centrally before reaching normal reading, Select chords, help and TOC. The D-pad and A/B/X/Y rotate with the reader; `-controller-layout=nintendo|xbox` selects the face-button convention, with Nintendo as the default and Xbox swapping A/B plus X/Y. `-no-control-rotation` keeps controls fixed while the reader rotates. Mirror axis is stored independently from reader rotation so mirror shortcuts do not rotate the UI or alter controller mapping. Document-state format 2 persists the axis while remaining able to read format 1.
- **Temporary action feedback overlay — implemented; device verification pending.** Controller shortcuts briefly display action labels on the existing transparent UI layer, including rotation, reading direction, night mode, mirroring, view reset, spread layout/order and dual-page mode. Notifications are transient and do not interrupt reading.
- **Remaining verification:** test the action messages on-device, including rapid successive actions replacing the current message and expiry while idle. The user reports performance is at least comparable and possibly faster, but no formal benchmark has been run.
- Do not merge automatically. Review the final diff and architecture after action-overlay verification.

- MuPDF display-list/page rendering cache.
- Tune MuPDF store/cache sizing and lifecycle.
- Optimise outline/TOC lookup.
- Profile and improve renderer performance and font-atlas handling.

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
