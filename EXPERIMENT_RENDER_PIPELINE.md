# Experimental logical framebuffer proof

This file documents an opt-in SDL presentation test on the `experiment-render-pipeline` branch. It is deliberately not the new PDF renderer.

## Purpose

Prove that SDL can present a framebuffer whose dimensions follow the reader's logical orientation, rotating the complete frame once at presentation time. The test pattern makes the logical top, bottom, left, and right edges visible.

## Run

Build this branch using the project's normal build instructions, then launch a PDF with:

```sh
GREEN_TEST_LOGICAL_FRAME=1 ./mugreen2 /path/to/document.pdf
```

The test mode bypasses PDF page rendering and normal UI rendering. It still opens the document so the reader's existing Rotate Right control (A) can change the test orientation. The logical framebuffer is recreated with swapped width/height for 90° and 270° rotations.

For a device with an upside-down mounted panel, also test:

```sh
GREEN_TEST_LOGICAL_FRAME=1 GREEN_ROTATE180=1 ./mugreen2 /path/to/document.pdf
```

Unset `GREEN_TEST_LOGICAL_FRAME` (or set it to `0`) to run the normal renderer.

## What this proves — and what it does not

It tests:
- logical framebuffer dimensions for 0°, 90°, 180°, and 270°;
- SDL's final whole-frame rotation and centring into the physical output;
- combination with the existing optional 180° hardware correction;
- resizing the logical framebuffer when reader rotation changes.

It does **not** test PDF rendering in logical coordinates, mirroring, fit/zoom, scrolling, dual-page layout, overlays, or performance of the proposed complete pipeline. Those remain later stages and must not be inferred from a successful test pattern.

## Acceptance checks

- The four labelled edges remain correctly oriented relative to the reader's logical frame at every rotation.
- The rotated image fills the physical output without stretching, clipping, or black borders on the target display.
- Rotating the reader repeatedly between portrait and landscape orientations resizes the framebuffer without failure.
- `GREEN_ROTATE180=1` adds exactly one further 180° correction.
- With the environment variable disabled, the existing renderer's presentation remains unchanged.
