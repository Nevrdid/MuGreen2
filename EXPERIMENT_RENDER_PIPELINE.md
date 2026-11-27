# Logical framebuffer renderer migration

This work remains isolated on `experiment-render-pipeline`. The branch now uses the logical framebuffer renderer as its **only rendering path**; the old SDL page renderer, rotation-specific spread renderer, and manual overlay compositing path have been removed from `sdl.c`. `main` is unchanged.

A safety snapshot of the fully tested parallel implementation is preserved on `experiment-render-pipeline-pre-cleanup`.

## Current rendering architecture

- PDF pages are rasterised in their unrotated page coordinate system into a logical framebuffer.
- Single-page and facing-page layout, fit, zoom, panning and clipping use logical viewport dimensions.
- A separate transparent ARGB layer holds the TOC, help panel and page indicator. It follows presentation rotation but is not mirrored with PDF content.
- The complete content and UI layers are rotated once during SDL presentation. Reader rotation and physical display mounting correction remain independent.
- Document mirroring is represented as a source texture flip before the final presentation transform.
- Controller button-down and button-up events pass through one input-normalisation layer before normal reading, Select chords, help, or TOC handling. D-pad directions and A/B/X/Y are rotated with the reader; Start, Select, Guide, and shoulders remain fixed.
- The default Nintendo-labelled face-button layout is retained. `-controller-layout=xbox` swaps A/B and X/Y before applying reader rotation; `-controller-layout=nintendo` selects the default explicitly.

## Controls and startup options

- `Select + Left`: rotate reader clockwise.
- `Select + Right`: rotate reader anti-clockwise.
- `-controller-layout=nintendo|xbox`: select the face-button convention at launch (Nintendo is the default).
- `Select + Up`: toggle vertical/horizontal spread stacking (dual-page mode).
- `Select + Down`: manually reverse facing-page order.
- `Select + L1`: toggle dual-page mode.
- `Select + R1`: toggle RTL reading.
- `L1 / R1`: previous / next page or spread.
- `L2 / R2`: skip 10 pages backward / forward.
- `X / Y`: zoom out / in.
- D-pad: pan, or change page when the relevant axis has no overflow or the page edge has been reached.

Startup options:
- `-rotation=0|90|180|270`: initial reader rotation.
- `-display-rotation=0|90|180|270`: rotate the complete presented frame for physical panel mounting.
- `-rtl` / `-ltr`: initial reading direction.

The old `GREEN_ROTATE180`, `GREEN_TEST_LOGICAL_FRAME` and `GREEN_TEST_LOGICAL_PDF` environment switches are no longer used. Run the branch normally:

```sh
./mugreen2 -rotation=0 -display-rotation=180 /path/to/document.pdf
```

## Rotation-aware controller mapping

Implemented on this branch; build and handheld verification are pending. The input layer applies the face-button permutation and D-pad rotation once, before routing events to the reader, TOC, help panel, and Select shortcuts. The Xbox layout swap is applied before reader rotation. `-no-control-rotation` disables the reader-rotation mapping while retaining the selected Nintendo/Xbox face-button convention. Mirror axis is now stored independently from reader rotation, preventing mirror shortcuts from rotating the UI or changing controller orientation. Document-state format 2 stores the mirror axis; format 1 states remain readable with the legacy axis default. Build and handheld verification are pending.

## Temporary action feedback

Controller shortcuts display a brief message near the bottom of the logical UI overlay. Messages last approximately 1.4 seconds, are replaced by a newer action, do not pause reading, and remain upright/unmirrored with the rest of the UI.

Current notifications include:
- Reader rotation clockwise/anti-clockwise.
- Manga mode / Regular mode.
- Night mode on/off.
- Horizontal/vertical mirror.
- View reset.
- Horizontal/vertical spread selection.
- Facing-page order swapped/restored.
- Dual-page mode on/off.

The overlay uses the existing transparent UI layer and SDL event timeout; it does not add a separate rendering surface or polling loop.

## Behaviour validated on the handheld before cleanup

The user confirmed:
- Logical framebuffer presentation and display-mount correction at tested orientations.
- Single-page rendering, panning, edge clamping and zoom centred on the viewport.
- Zoom continues to work after reset and reader rotation.
- L1/R1 and D-pad page navigation across rotations, including distinct vertical and horizontal direction mappings.
- Facing-page mode with horizontal stacking by default and a persistent Select+Up vertical-stack toggle; page order, RTL and manual Select+Down override.
- TOC placement/orientation, help panel, page indicator and unmirrored UI overlays.
- L2/R2 ten-page skipping in single-page and dual-page modes, including the beginning and end of documents.
- Select+Left/Right reader rotation.

The user reports performance is at least comparable to the previous implementation, possibly faster. This is a subjective device observation, not a formal benchmark.

A small non-blocking inconsistency remains: when navigating to previous spreads, the expected entry corner is not always applied and can occasionally land at top-left. The user considers navigation comfortable and has deferred fixing this.

## Cleanup status

The first cleanup pass promoted the logical renderer to the default on this branch and removed the parallel legacy SDL implementation. `sdl.c` went from 2,768 lines before cleanup to 2,268 lines after the current pass. This is a source-size comparison, not a measure of architectural quality by itself.

The old test-mode switches and stage-1 pattern renderer are gone. Legacy SDL spread geometry, zoom, page rendering, rotation-aware scrolling, and temporary overlay-surface/compositing helpers were removed. The logical rendering path now owns presentation, overlays and navigation.

## Remaining work before considering a merge

1. Build the cleaned branch and run a complete device smoke test. The cleanup has not yet been compiled or tested on-device.
2. Specifically recheck RTL view entry after toggling RTL at 0°, 90°, 180° and 270°; the logical page-start mapping was adjusted during cleanup and needs device verification.
3. The PDF backend API has now been simplified: `pb_render` and `pb_device_size` no longer accept rotation or mirroring arguments. All in-repository call sites were updated to use the unrotated API.
4. Optionally collect a repeatable performance comparison. The user's current impression is that performance is unchanged or improved.
5. Review the final diff and architecture before deciding whether to merge. Do not merge automatically.
