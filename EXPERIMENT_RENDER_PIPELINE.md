# Experimental logical framebuffer pipeline

This experiment lives only on `experiment-render-pipeline`. It is deliberately isolated from `main`.

## Stage 1 — SDL presentation proof

The opt-in `GREEN_TEST_LOGICAL_FRAME=1` mode draws a labelled test pattern into a framebuffer whose dimensions follow the reader's logical orientation. SDL rotates the complete framebuffer once at presentation time.

Run:

```sh
GREEN_TEST_LOGICAL_FRAME=1 ./mugreen2 /path/to/document.pdf
```

For a panel mounted upside down, also test:

```sh
GREEN_TEST_LOGICAL_FRAME=1 GREEN_ROTATE180=1 ./mugreen2 /path/to/document.pdf
```

The test pattern bypasses PDF rendering and the normal UI. It still opens the document so the reader's existing rotation control can change the test orientation.

**Device result:** the user confirmed that the complete pattern rotates correctly through the tested reader orientations and that `GREEN_ROTATE180=1` adds the expected startup 180-degree correction. One test-pattern label was misplaced inside the black centre area; this is a cosmetic label-coordinate issue, not a reported presentation-transform failure.

## Stage 2 — single-page PDF rendering proof

The opt-in `GREEN_TEST_LOGICAL_PDF=1` mode renders one real PDF page without asking MuPDF to rotate it. The PDF is drawn in the logical framebuffer using its unrotated page dimensions; SDL applies reader rotation to the completed framebuffer at presentation time.

Run:

```sh
GREEN_TEST_LOGICAL_PDF=1 ./mugreen2 /path/to/document.pdf
```

Optionally combine with the hardware correction:

```sh
GREEN_TEST_LOGICAL_PDF=1 GREEN_ROTATE180=1 ./mugreen2 /path/to/document.pdf
```

In this probe:
- **B / A (SDL button names)** retain the reader's existing rotate / reset actions. Physical labels depend on the controller's SDL mapping; on the user's Nintendo-layout controller, physical **A** rotates and physical **B** resets the view.
- **L1 / R1** move to the previous / next page. In the logical-PDF probe, their page direction is reversed at 90° and 180° so the physical left/right behaviour stays consistent; RTL reading reverses it again.
- **L2 / R2** are ignored in this probe; the normal ten-page skip action is intentionally disabled.
- The reset action returns to width-fit and the top of the page.
- **X / Y** zoom out / in, using a logical-coordinate zoom anchored at the viewport centre.
- Mirroring is translated to a pre-presentation SDL flip so its existing post-rotation vertical-mirror result can be retained; panning direction is transformed through the same flip.

## Stage 3 — logical-coordinate single-page panning

The same `GREEN_TEST_LOGICAL_PDF=1` probe supports D-pad panning. Physical D-pad directions are transformed through the inverse reader rotation and the source mirror flip, so movement follows the direction pressed on the handheld. The current page is panned in logical coordinates, with page and viewport bounds calculated in the same logical orientation. Existing D-pad hold-repeat timing is reused.

## Stage 4 — logical-coordinate zoom

The logical-PDF probe now supports the existing X/Y zoom buttons. Zoom is calculated against the logical framebuffer and keeps the page point under the viewport centre anchored as the scale changes. It does not call the legacy rotation-aware zoom routine. Zoom is bounded to the same 0.05–20.0 fine-scale range used by document state.

**Device result:** the user built and tested the probe. X/Y zoom in/out, viewport-centred zoom, horizontal panning after zooming, and panning at 90° and 270° all work. The user also confirmed that zoom remains available after resetting the view and after changing reader rotation. The framebuffer pointer is refreshed after rendering because rotation can recreate the logical framebuffer.

## Stage 5 — page-edge transitions

The logical-PDF probe now changes pages when the user presses farther past an edge that has real scrollable overflow. Reaching the edge itself only clamps the current page; a subsequent press beyond that edge attempts the page change. Each axis is handled independently: when the page dimension is less than or equal to the logical viewport dimension, D-pad input on that axis changes page immediately because there is no scrolling to perform. When the page is larger than the viewport on that axis, D-pad input pans until the corresponding edge is reached, and only a further press changes page.

Horizontal D-pad page direction follows L1/R1: Left matches L1 and Right matches R1, including the 90°/180° rotation reversal and RTL adjustment. Vertical D-pad page direction is mapped independently: Up/Down mean previous/next at 0° and 90°, and next/previous at 180° and 270°; RTL reverses that mapping. A successful next-page transition starts at the logical top-left; a previous-page transition starts at the logical top-right, based on the actual page change rather than the physical button. Overscroll is not carried to the new page.

Page dimensions are measured unrotated against the logical framebuffer dimensions. This is important at 90°/270°, where the logical viewport width and height are swapped relative to the physical display. The transition remains limited to the experimental single-page path.

**Device result:** the user has now tested the Stage 5 page-edge behaviour and confirmed that it works, including the final fit/overflow and rotation-specific direction refinements. The next/previous page entry corners also behave as intended. The broader mirror/rotation matrix remains part of the overall pipeline validation.

This is intentionally a single-page rendering probe. It does **not** yet migrate or validate:
- dual-page/facing-page layout (the current page is shown alone);
- TOC, help, page indicator, or other overlays;
- all mirror/rotation combinations, window resizing, or performance.

The normal renderer remains the default when neither test variable is enabled. The stage 1 test takes precedence if both variables are set.

## What to check on device

1. Use a PDF with clear page orientation (ideally a page containing text and obvious top/bottom content).
2. Compare 0°, 90°, 180°, and 270° reader orientations with the same PDF.
3. Confirm text is upright and the page is not rotated twice or mirrored unexpectedly.
4. Turn pages with L1/R1 at 0°, 90°, 180° and 270°. L1/R1 should reverse at 90° and 180°; also check the expected reversal in RTL mode, and confirm the page remains correctly positioned.
5. Zoom in and out with X/Y. Confirm the zoom stays centred around the middle of the visible area and that zooming creates enough overflow to test both D-pad axes.
6. Pan with each D-pad direction at all four rotations. Movement should follow the physical direction pressed, including when holding a direction to repeat. The first press that reaches an edge should only clamp; a further press should change page if that axis has real overflow.
7. On a page change, verify that next pages start at logical top-left and previous pages at logical top-right. Confirm Left/Right follow L1/R1, and Up/Down follow their documented vertical mapping (previous/next at 0° and 90°, next/previous at 180° and 270°), including RTL.
8. Repeat the panning and page-edge checks with mirroring enabled, if desired, to check that the direction mapping follows the mirrored content.
9. Repeat with `GREEN_ROTATE180=1` and confirm it adds only the physical panel correction.
10. Confirm the ordinary renderer still behaves exactly as before when both test variables are unset.

This remains an experimental single-page path, not a replacement for the existing renderer. The user has build-tested and device-tested the current single-page rendering, zoom, panning, and page-edge transitions. Dual-page spreads, overlays, complete mirroring coverage, and performance remain outstanding. Keep the experiment isolated until those areas are migrated and tested, then compare complexity and behaviour against the existing renderer before considering any merge.


## Stage 6 — logical facing-page spreads

The logical-PDF test now renders facing-page pairs in one continuous logical-coordinate spread. The stack preference is intended to remain visually stable from the user's point of view: horizontal means left/right and vertical means top/bottom at every reader rotation. The latest correction removes the rotation-dependent stack-axis toggle; it still needs device verification because the previous implementation visibly changed axes when rotating. Slot ordering remains rotation-aware: horizontal stacking reverses slots at 180°/270°, while vertical stacking reverses slots at 90°/180°; Select+Down applies the manual slot-order override on top.

- **Select + Up** toggles horizontal and vertical stacking while dual-page mode is active in the logical-PDF test. The preference is saved in the global settings file and is restored on the next launch.
- Page pairs retain RTL ordering and the existing manual spread-order override. The slot order is adjusted for the combination of reader rotation and selected physical stack axis.
- Mixed page dimensions are centred across the cross-axis. The fit scale is calculated against the combined spread dimensions, respecting the document's fit mode.
- X/Y zoom uses the combined spread geometry but preserves the point under the **viewport centre**. It does not anchor around the centre of the entire spread.
- D-pad input pans the combined spread in logical coordinates; once the relevant axis reaches its edge (or fits entirely in the viewport), another press changes to the adjacent page pair. L1/R1 also navigate by pairs.
- An unpaired first/last page falls back to the single-page logical renderer.

**Build/device verification pending.** The user tested the initial implementation and found the visible stack axis changed with reader rotation (horizontal at 0°/180°, vertical at 90°/270°). A source-level correction now keeps the selected axis unchanged across rotations. The page-order mapping was also corrected based on the user's earlier test matrix, but both corrections need device retesting. Verify the visible axis at all four rotations in both modes, Select+Up, persistence, page order in LTR/RTL and manual Select+Down reversal, mixed-size pages, zoom anchoring while focused on one page, D-pad pan/edge transitions, and unpaired pages. This stage has not yet been compiled or tested on the handheld.

The logical-PDF test still does not migrate TOC/help/page-indicator overlays. The normal renderer remains unchanged when GREEN_TEST_LOGICAL_PDF is unset. Select+Up's new stack toggle is intentionally active only in the logical-PDF experiment.
