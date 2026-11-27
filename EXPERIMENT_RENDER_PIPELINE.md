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
- **B** rotates the reader clockwise (the existing implementation's rotation binding).
- **L1 / R1** move to the previous / next page.
- **D-pad** pans the current page in physical screen directions, transformed into logical page coordinates; movement clamps at page edges and does not change pages.
- Holding a D-pad direction uses the existing repeat timing.
- **L2 / R2** are ignored in this probe; the normal ten-page skip action is intentionally disabled.
- **A** resets the page to the width-fit view and returns the pan to the origin.
- Zoom shortcuts are disabled in this probe because the existing zoom-anchor calculations still assume the physical/rotated viewport.
- Mirroring is translated to a pre-presentation SDL flip so its existing post-rotation vertical-mirror result can be retained; panning direction is transformed through the same flip.

This is intentionally a single-page rendering probe. It does **not** yet migrate or validate:
- page-edge transitions or page-change scroll anchoring;
- zoom anchoring and zoom behaviour beyond the initial width-fit calculation;
- dual-page/facing-page layout (the current page is shown alone);
- TOC, help, page indicator, or other overlays;
- all mirror/rotation combinations, window resizing, or performance.

The normal renderer remains the default when neither test variable is enabled. The stage 1 test takes precedence if both variables are set.

## What to check on device

1. Use a PDF with clear page orientation (ideally a page containing text and obvious top/bottom content).
2. Compare 0°, 90°, 180°, and 270° reader orientations with the same PDF.
3. Confirm text is upright and the page is not rotated twice or mirrored unexpectedly.
4. Turn pages with L1/R1 and confirm the page remains correctly positioned.
5. Pan with each D-pad direction at all four rotations. Movement should follow the physical direction pressed, including when holding a direction to repeat; movement should stop cleanly at each page edge without changing pages.
6. Repeat the panning checks with mirroring enabled, if desired, to check that the direction mapping follows the mirrored content.
7. Repeat with `GREEN_ROTATE180=1` and confirm it adds only the physical panel correction.
8. Confirm the ordinary renderer still behaves exactly as before when both test variables are unset.

This stage is not a replacement for the existing renderer and must not be merged based only on the test-pattern result. Build and device verification are still required before proceeding to scrolling, zoom, spreads, or overlays.
