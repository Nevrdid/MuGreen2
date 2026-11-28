# Pending Tests

This file is the practical verification queue for MuGreen2. Keep it in sync with [ROADMAP.md](ROADMAP.md): the roadmap records scope and implementation history; this file records only checks that still need to be run or re-run.

## How to use this list

- Check an item only after the relevant build/runtime/device test has actually passed.
- A commit or code review is not a test result.
- If a test fails, leave it unchecked and add a short **Observed failure** note with the document, configuration, and steps needed to reproduce it.
- Remove completed items progressively, but preserve important failures/regressions in the relevant roadmap section until fixed.
- Tests marked **retest** cover behaviour that passed earlier but may have been affected by later renderer/input refactors.
- Unless stated otherwise, these are local build/device checks; no such checks are implied by this file's creation.

## Phase 1 — Build-system reliability

- [ ] Build and run the ARMHF target. Native x86_64 and AArch64 builds were reported verified; ARMHF is the remaining architecture check.

## Phase 2 — State, persistence, and failure robustness

### Batch B — document-open and render-failure handling

- [ ] Run as a non-root user (or use `setpriv`) and verify an unreadable PDF exits with code `3`.
- [ ] Open a password-protected PDF and verify exit code `5`.
- [ ] Use a PDF with a deliberately broken middle page: verify the placeholder is shown, navigation can move past it, and reopening the document does not get stuck on that page.
- [ ] Test a dual-page spread containing one broken page and one valid page.
- [ ] Trigger a recoverable content/image error and verify both the on-screen warning marker and corresponding log entry.
- [ ] Test an empty PDF and PDFs with malformed or non-positive page dimensions; verify safe rejection/fallback without a crash.
- [ ] If a suitable sample is available, exercise the render-failing PDF/page path and verify its stable exit/recovery behaviour.
- [ ] Recheck invalid-path diagnostics in `<datadir>/mugreen2.log` separately from invalid CLI-option diagnostics.

### Batch C — dimensions and SDL controller mappings

- [ ] Build with the target toolchain after the Batch C changes.
- [ ] Verify `-width` and `-height` each accept `0`, `1`, and `65535`.
- [ ] Verify both options reject `65536`, negative values, empty values, alphabetic input, and trailing characters with usage exit code `1`.
- [ ] Verify width and height can independently use automatic sizing when set to `0` (for example, fixed width with automatic height).
- [ ] Verify a known unmapped controller is recognised when a valid mapping is supplied through `SDL_GAMECONTROLLERCONFIG`.
- [ ] Repeat the mapping test using `SDL_GAMECONTROLLERCONFIG_FILE`.

### Persistence and settings retest

- [ ] Rebuild and confirm the dual-page preference is saved immediately when toggled and restored on the next launch.
- [ ] Recheck document state after the strict state-format/parser changes: page, scale, X/Y offsets, rotation, and fit mode restore correctly; malformed/truncated/trailing state is rejected safely.
- [ ] Recheck state saving under normal exit and forced termination after the debounce/atomic-write changes.

## Corrective rendering fix — continuous dual-page spread

- [ ] Build the current renderer and run the spread regression set below.
- [ ] Zoom in/out repeatedly at each reader rotation in both LTR and RTL; the content under the screen centre must remain anchored.
- [ ] Repeat zoom-anchor checks after panning either page to the centre, at different pan positions and near all viewport edges.
- [ ] Rotate the handheld to match each reader rotation and confirm the first page appears on the reading side at every angle.
- [ ] Confirm Select+B still toggles night mode.
- [ ] Verify vertical/horizontal spread-layout toggling and persistence. (The former Select+D-pad Down manual slot-swap test is obsolete: manual spread swapping was removed.)
- [ ] Recheck LTR/RTL ordering, mixed page sizes, first/last-page cases, dual-page toggling, and restored state.

## Phase 3 — Reading and navigation

### Batch A and TOC

- [ ] Build the current Phase 3/TOC changes; no build verification is recorded yet.
- [ ] With a PDF containing an outline, test TOC D-pad selection, expand/collapse, and hold-repeat at 0°, 90°, 180°, and 270° with control rotation enabled.
- [ ] Repeat TOC navigation at all four rotations with `-no-control-rotation`; D-pad directions should remain physical while the panel itself remains rotated.
- [ ] Test TOC open/close, selection, expand/collapse, and other state transitions across rotations.
- [ ] Hot-plug or disconnect the controller while TOC repeat is active; confirm repeat is cancelled cleanly.
- [ ] Confirm normal-reading hold-repeat, release behaviour, default 0.9 scroll step, `-step` override, and unpaired-final-page indicator still work after later input/render changes.

### Remaining planned Phase 3 behaviour (implementation work, not yet a test-only item)

- Synchronise the selected TOC section with the visible page and open the TOC at the current section.
- Add fit-mode cycling and a direct go-to-page/percentage workflow.
- Add zoom hold-repeat while preserving Select-hotkey detection.
- Add controller-presence/mapping feedback for both absent controllers and attached-but-unmapped joysticks.
- Keep controller press/release semantics, modifier chords, triggers/axes, and bindings consistent across reading, TOC, and help overlays.

## Phase 4 — Rendering and performance

### Current correctness/interaction backlog

- [ ] **Page-turn entry rule (latest implementation):** test the destination spread's rendered page centres. Select the page furthest toward the top; when vertical centres tie, choose the page furthest toward the reading side (right in LTR, left in RTL). Enter at that page's top reading-side corner (top-right in LTR, top-left in RTL). Test vertical and horizontal spreads, mixed page dimensions, forward/backward turns, LTR/RTL, and all viewport edges.
- [ ] Specifically retest `ps2_recommended.pdf`: backward from page 3 to the 1–2 spread in horizontal layout, and the equivalent vertical layout. Earlier reports described landing at the top edge of page 2 in the horizontal case and an out-of-page horizontal offset in the vertical case; the latest geometry/RTL-aware changes have not yet been device-verified.
- [ ] Confirm the partner page remains reachable by panning when it initially extends outside the viewport.
- [ ] In single-page RTL, verify horizontal arrival side follows the direction of the key pressed.
- [ ] Exhaustively retest dual-page zoom anchoring across horizontal/vertical spreads, different pan positions, all edges, and mixed page sizes. Quick verification passed earlier, but the full matrix was not completed.
- [ ] Test an odd-page document's unpaired final page in horizontal and vertical layouts, all fit modes, zoom/pan, LTR/RTL, and after page skipping.
- [ ] Verify simplified spread ordering: horizontal LTR and RTL, vertical document order in both directions, D-pad progression, shoulder navigation, page-turn entry, and reader rotations. Manual page-order override and mirroring controls were removed; do not test the removed shortcuts as features.

### UI feedback and rotation retests

- [ ] Verify action-feedback messages are replaced correctly by rapid successive actions.
- [ ] Verify action-feedback messages expire while the reader is idle without leaving stale UI.
- [ ] Recheck page indicator, TOC, and help overlay placement/readability at all reader rotations after the latest cleanup/build.

### Phase 4 cleanup/build

- [ ] Build after the Phase 4 code/documentation cleanup pass (removal of legacy helpers, logical-fit helper, launcher display-rotation option, and related cleanup). The roadmap explicitly records this build as pending.

### Performance and resource-lifecycle backlog

These are still implementation/performance-review tasks, not tests of completed features. Once implemented, add their concrete verification cases here.

- [ ] Separate content invalidation from overlay-only invalidation so overlay expiry does not rerasterise PDF pages.
- [ ] Skip empty overlay texture uploads.
- [ ] Prefer an accelerated SDL renderer with fallback; compare accelerated/vsync and software/window-surface paths on target hardware.
- [ ] Implement and measure strip/tile rendering for scrolling.
- [ ] Avoid unnecessary framebuffer recreation and exercise allocation-failure handling.
- [ ] Resolve TOC page destinations lazily and measure large-outline opening/selection.
- [ ] Pre-render bitmap font glyphs into an atlas and measure overlay redraw cost.
- [ ] Cache page bounds independently of display lists; then assess a small display-list cache.
- [ ] Profile page-turn latency with different MuPDF store sizes; evaluate a larger dual-page default.
- [ ] Expose anti-aliasing as an `-aa=` option and verify its effect.
- [ ] Make render-failure diagnostics actionable and bounded: log once per page, add timestamps and log size/rotation policy, and align README claims with actual behaviour.
- [ ] Make `-step` accept documented decimal values (or correct the documented syntax/default); verify parsing matches documentation.

## Notes on superseded checks

- The old Select+D-pad Down manual spread-slot swap test is superseded by removal of manual swapping.
- Page mirroring and Select+X/Select+Y mirror tests are superseded by removal of page mirroring.
- The current page-turn rule is reading-direction-aware: LTR enters at the top-right corner; RTL enters at the top-left corner. Do not use the older direction-independent corner wording from historical notes.
