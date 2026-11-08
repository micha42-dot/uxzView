# Review corrections — 2026-11-09

Baseline: `cf98c85` (reviewed against the November 3 state). The review reproduced
an application abort, lost JPEG descriptions, two deletion-navigation policy
violations, incorrect preview line wrapping and wasted uncacheable prefetch.
The generated diagnostics and original review remain under `build/review20261109*`.

## Speculative loading

`prefetchImage` owns the best-effort exception boundary in `core/image_loading.*`.
It never throws or presents an error dialog. Real opens keep their existing
diagnostics. The window keeps one low-priority worker and clears obsolete queued
work; running native decodes still stop only at existing pipeline boundaries.

Header/revision reads occur on that worker. Only ordinary JPEG/PNG/BMP aliases
are eligible; RAW/PDF/animated/other format work is not started speculatively.
Eligible pixel buffers must fit both the image-read guard and a 64 MiB target
limit, stay below the spill threshold and fit the cache's current spare space.
`ImageLoadCache::speculativeAllowance` reserves 64 KiB for properties and does
not count retained originals/presentations/previews as free. Actual admission
continues through the normal revision/epoch/cache-cost checks. Large metadata
and concurrent budget changes can still affect final admission; this is not a
global process-memory limit. The startup prewarm policy is unchanged.

Regressions cover a throwing decoder, zero/small cache, oversized targets and
cancellation before decode. A fresh 32 MiB subprocess opens a 64x64 image next
to a 1536x1536 image with display sharpening enabled. The latter's working-set
exception must be contained; the first document stays installed and the child
exits normally. The baseline aborts in this case.

## JPEG descriptions and native bytes

The JPEG segment scanner records COM markers. Such files retain Qt's text-aware
reader; the full-buffer read rechecks this decision if the file changed after
acceptance. Fallback finalization transfers pixel text to `ImageDocument::text`.
Tests compare the Description read by Qt with the viewer document, then save as
PNG and read it back, both with and without an ICC profile. Direct fallback
calls are included, covering the second eligibility check.

The ordinary TurboJPEG path now requests BGRA rather than BGRX. The public
library contract guarantees opaque alpha for BGRA; RGB32's upper bytes therefore
have defined values without an extra pixel pass. Pixel/stride tests remain in
the large-image suite.

## Deletion and navigation

The black deletion state stores its old position and non-wrapping neighbour
anchors. `resumeAfterRemoval` locates that slot in the current directory snapshot
and returns the same `FolderTraversal` used for normal arrows. Requests carry
that traversal through unreadable-file failures, so Skip can advance without a
popup. The wrap preference controls both ends and both directions; a no-wrap
edge stays black. Auto-open-next deletion also carries traversal context.
Bounded retry/direction/wrap/changed-list cases are covered independently of the
OS trash service. Windows end-to-end probes use only generated temporary images.

## Text preview

Final editing and preview share one text-paint operation with the original point
size, text rectangle, style and image DPI. The preview applies its viewport
transform after layout, avoiding integer rounding/minimum-point-size changes.
It crops around the text area and bounds both raster axes to 1024 pixels.
The dialog caches that background until its viewport changes; text/style changes
copy and redraw only the bounded raster. The label ignores pixmap size hints
and rebuilds on resize.

Tests compare a selected-region preview at 1:1 with pixels cropped from the
actual full-image edit. Small/large fonts, 6000/16000-pixel source widths and
96/300 DPI are included. An 800x20000 portrait checks bounded output and source
color; the dialog test checks that resizing keeps the pixmap inside its viewport.

## Validation scope

The local build uses Windows 11, GCC 16.2 and Qt 6.11.2. Native Windows 10,
Linux/macOS execution and broad visual/compositor checks remain separate work.
No storage key, profile format, extension ABI or version number changes.
