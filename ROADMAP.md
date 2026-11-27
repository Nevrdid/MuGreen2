# MuGreen2 Roadmap

This roadmap tracks implementation scope and verification status. Items are not considered tested merely because their code has been committed; device/build verification is recorded separately.

## Phase 1 — Build-system reliability

**Status: Complete** (native x86_64 and AArch64 builds verified by the user; ARMHF remains untested).

- Compiler/sysroot detection and architecture-specific build configuration.
- MuPDF and SDL2 dependency build scripts, pinned source checksums, and separate install prefixes.
- Dynamic SDL2 linking and runtime compatibility documentation.
- Install targets, `PREFIX`, `DESTDIR`, and `NOSTRIP`.

## Phase 2 — State, persistence, and failure robustness

**Status: In progress — Batch A verified by the user; Batch B implemented, local verification pending.**

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
- Document state survives forced termination after the debounce fix.
- Dual-page preference persistence still needs local rebuild/runtime confirmation.

Remaining hardening batches:
- **Batch A — implemented and verified by the user:** unified data directory, new settings path without legacy migration, canonical document identity, writable-directory validation/fallback reporting, path-only input normalization, and cached document-state directory.
- **Batch B — implemented, needs local verification:** report MuPDF page-render failures instead of silently presenting incomplete frames; classify document-open failures (missing, unreadable, invalid/unsupported, password-protected, and other backend/I/O failures); provide stable process exit codes and document them in the README.
- **Batch C — pending:** validate width/height before conversion to `unsigned short` (preserve `0` for automatic size; accept `0..65535`) and document `SDL_GAMECONTROLLERCONFIG` / `SDL_GAMECONTROLLERCONFIG_FILE`. The controller detection/banner implementation remains in Phase 3.

No legacy settings/state migration is planned unless a concrete production requirement appears.

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
