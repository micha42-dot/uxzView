# Large-image storage review — 1 November 2026

Phase 1 is source commit `42558bf`. Subsequent work remains uncommitted at the
user's request. No push was performed. Routine AV scans were discontinued at
the user's request; subsequent verification concerns build/runtime correctness.

## Changes

- Replace the fixed RAM ceiling with startup half-RAM sizing, 512 MiB to 8 GiB,
  a manual override, checked one-million-axis geometry and live resize limits.
- Bound 8-/16-bit resampling to converted rows, horizontal cache and vertical
  accumulation. Preserve legacy rounding and native nearest-neighbor pixels.
- Back oversized originals/results with reserved mapped files; account active
  documents, results, display and Undo against one per-process disk budget.
  Last-reference release removes files. Crash cleanup preserves active process
  locks, links and unknown data. Unix storage is owner-only. Release the root
  creation gate before potentially slow physical reservation.
- Decode PNG and sequential JPEG into unique mapped destinations, apply EXIF
  orientation without full RAM copies, and permit native JPEG scaled previews.
- Stream large PNG/JPEG output from original pixels via QSaveFile. Bound row
  work, retain target revision guards, flatten alpha per row, and warn before
  omitting full encoded EXIF/IPTC/XMP rewriting. Share this policy with the GUI's
  post-save metadata state. Prepare selection exports in the existing GUI-safe
  worker join without introducing another progress window.

## Verification

Windows 11, GCC 16.2.0, Qt 6.11.2: **596 Qt cases in nine suites and 46 Python
checks**, no failures or skips. The same 596 Qt cases pass against the deployed
portable runtime with the SDK removed from PATH. Real OCR/QR processes, all five
OCR language packages, profile/tool relocation and installed modules pass.
Native navigation, settings, resize coupling and mapped-image painting pass at
100/125/150/200 percent scaling.

The dedicated storage suite has 36 cases including forced real mappings,
reference comparisons for all seven filters across native 8-bit and arbitrary
16-bit samples, cancellation, lifetime/worker release, budget/geometry errors,
metadata consent, ICC conversion, all eight JPEG EXIF orientations, crash cleanup
and active-lock preservation. A 100,001 × 512 image (~195 MiB pixels) is written
as PNG, read back into a mapping and compared exactly; fitted painting checks
both colored halves and original-coordinate selection. The 100,000-square,
37.25 GiB example is checked arithmetically, not allocated as a test fixture.

Evidence: `build/review-memory-phase2-verified-validation-summary.json`,
`build/memory-runtime-summary.json`, `build/large-image-test-results.*`,
`build/portable-large-image-test-results.*` and `build/memory-large-image-*.txt`.
README images and the Windows resource template remain unchanged. Canonical
runtime/source archives are verified separately during release packaging.

## Limits

Native Windows 10, Linux and macOS runs remain unverified locally. The OS-specific
RAM, disk reservation and permission branches are implemented, but Windows tests
do not validate their Unix execution. Progressive large JPEG, arbitrary huge
codecs, non-streaming edits and display sharpening retain explicit limits.
JPEG output ends at 65,500 pixels per axis. ICC display conversion may require a
second mapping. The budgets are not a strict whole-process RSS cap.

See [large images](LARGE_IMAGES.md) for settings, ownership contracts and the
16 GiB RAM / 64 GiB backing example. The earlier [resource review](RESOURCE_REVIEW_2026-11-01.md)
records the measured 3.0–3.21× grayscale component speedup and ~7.66 MB runtime
reduction; those are component measurements, not a whole-viewer speed claim.
