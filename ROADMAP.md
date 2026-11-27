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
- Add a manual session-only override on Select+D-pad Down (dual-page mode only) for devices whose physical rotation direction differs; it resets the spread pan to a valid origin when toggled.

**User verification:** the continuous spread zoom/scroll issue and the 180° spread-slot compensation are reported fixed. The 90° compensation and manual Select+B override are newly implemented and need device testing: rotate the PDF 90° with the reader control, physically turn the handheld 90°, and confirm the first page appears at the physical top; confirm 180° remains correct and 270° remains unchanged. Test Select+D-pad Down in dual-page mode, confirm it reverses the slots and a second press restores them, and confirm it does not affect single-page mode. Also test LTR/RTL, mixed page sizes, first/last single-page cases, dual-page toggling, and restored state.

## Phase 3 — Reading and navigation

**Status: Planned / review existing implementation before changing it.**

- Page indicator and page feedback.
- Scrolling overlap/step behaviour.
- Controller repeat behaviour and navigation consistency.
- Synchronise the current TOC section with the visible page.
- Fit modes and direct go-to-page workflow.
- Keyboard fallback using shared logical button-down/button-up handlers rather than duplicated navigation logic.
- Controller presence and mapping feedback:
  - distinguish no physical joystick from an attached joystick without an SDL GameController mapping;
  - show an appropriate on-screen message;
  - support/document mapping configuration for handheld controllers.
- Keyboard/controller input architecture should account for press/release semantics, modifier chords, triggers/axes, and overlays.

Previously implemented navigation work to preserve:
- Rotation behaviour across orientations.
- Scrolling and page changes respecting rotation.
- Manga/Japanese right-to-left page ordering and direction.
- Controller navigation and edge-triggered page changes.

Review current `main` before implementing any item; some listed functionality may already be present or partially complete.

## Phase 4 — Rendering and performance

**Status: Planned.**

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

- General code organisation after Phase 3 input/navigation interfaces stabilise.
- Consolidate controller/keyboard help and derive help text from the actual bindings where practical.
- Remove avoidable duplication and clarify module boundaries.
- Keep licensing and third-party notices accurate; update documentation as features land.

Keyboard fallback is implemented in Phase 3; Phase 6 covers any remaining structural cleanup, not a second input rewrite.

## Phase 7 — Presentation polish

**Status: Complete.**

- Minor mandatory presentation fixes completed and committed.
