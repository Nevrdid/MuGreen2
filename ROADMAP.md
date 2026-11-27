# MuGreen2 Roadmap

This roadmap tracks implementation scope and verification status. Items are not considered tested merely because their code has been committed; device/build verification is recorded separately.

## Phase 1 — Build-system reliability

**Status: Complete** (native x86_64 and AArch64 builds verified by the user; ARMHF remains untested).

- Compiler/sysroot detection and architecture-specific build configuration.
- MuPDF and SDL2 dependency build scripts, pinned source checksums, and separate install prefixes.
- Dynamic SDL2 linking and runtime compatibility documentation.
- Install targets, `PREFIX`, `DESTDIR`, and `NOSTRIP`.

## Phase 2 — State, persistence, and failure robustness

**Status: In progress — final hardening batch planned.**

Already implemented:
- Strict document-state parsing and validation.
- Bounded restored zoom and sanitised scroll offsets.
- Atomic state/settings writes, fsync, and rename.
- Debounced document-state saving, including saving despite a continuously active SDL event queue.
- URI-safe state identity encoding.
- Global night-mode, right-to-left, and dual-page preferences.
- Dual-page preference is saved immediately when toggled.
- Legacy URI migration code and its documentation were removed.

User verification:
- Document state survives forced termination after the debounce fix.
- Dual-page preference persistence still needs local rebuild/runtime confirmation.

Final hardening batch:
1. **Unified data directory:** introduce `MUGREEN2_DATA_DIR`; otherwise use `XDG_DATA_HOME/mugreen2`, then `$HOME/.local/share/mugreen2`, then a writable directory beside the executable as a last resort. If no usable location exists, warn once on stderr and continue without persistence. Settings and document state must use the same resolved directory.
2. **New settings location:** store global preferences at `<datadir>/settings`; do not migrate or read legacy `~/.green.settings`. Backward compatibility is intentionally unnecessary before production.
3. **Canonical document identity:** resolve document paths with `realpath()` before URI encoding so equivalent paths share state. If resolution fails, retain a safe absolute/raw-path fallback.
4. **Render failure reporting:** propagate device-size and render failures, log them, and show a clear in-page error instead of silently displaying a blank/partial page. Avoid logging the same persistent failure every frame.
5. **Open failure classification:** distinguish missing/unreadable files, password-required documents, malformed/unsupported documents, and other backend/I/O failures. Provide documented, stable process exit codes for frontends.
6. **Controller mapping documentation:** document `SDL_GAMECONTROLLERCONFIG` and `SDL_GAMECONTROLLERCONFIG_FILE`, including that mappings must be available before SDL GameController initialisation. Controller detection/banner implementation is deferred to Phase 3.
7. **Window dimension validation:** validate parsed width/height before conversion to `unsigned short`; preserve `0` as automatic display size and accept only `0..65535`.

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
