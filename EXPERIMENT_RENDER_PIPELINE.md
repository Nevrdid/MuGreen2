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

The logical-PDF probe now changes pages when the user presses farther past an edge that has real scrollable overflow. Reaching the edge itself only clamps the current page; a subsequent press beyond that edge attempts the page change. A non-scrollable axis never triggers an immediate page change.

The page direction is shared with L1/R1: physical Left/Up use the same direction as L1, and Right/Down use the same direction as R1, including the 90°/180° rotation reversal and RTL adjustment. A successful next-page transition starts at the logical top-left; a previous-page transition starts at the logical top-right. Overscroll is not carried to the new page.

Page dimensions are measured unrotated against the logical framebuffer dimensions. This is important at 90°/270°, where the logical viewport width and height are swapped relative to the physical display. The transition remains limited to the experimental single-page path.

**Device verification pending:** test all four D-pad directions at all four rotations, with pages that overflow horizontally and vertically. Confirm that reaching an edge does not change page until the next press, that the next press changes in the same direction as L1/R1, that next/previous pages start at the requested corners, and that first/last page boundaries do not wrap. Repeat with RTL and mirroring.

This is intentionally a single-page rendering probe. It does **not** yet migrate or validate:
- dual-page/facing-page layout (the current page is shown alone);
- zoom anchoring beyond the viewport-centre zoom implemented in Stage 4;
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
7. On a page change, verify that next pages start at logical top-left and previous pages at logical top-right. Confirm Left/Up match L1 and Right/Down match R1 at all rotations, including RTL.
8. Repeat the panning and page-edge checks with mirroring enabled, if desired, to check that the direction mapping follows the mirrored content.
9. Repeat with `GREEN_ROTATE180=1` and confirm it adds only the physical panel correction.
10. Confirm the ordinary renderer still behaves exactly as before when both test variables are unset.

This remains an experimental single-page path, not a replacement for the existing renderer. The user has build-tested and device-tested the current single-page rendering, zoom, and panning path, but this does not validate page-edge transitions, dual-page spreads, overlays, complete mirroring coverage, or performance. Keep the experiment isolated until those areas are migrated and tested, then compare complexity and behaviour against the existing renderer before considering any merge.
