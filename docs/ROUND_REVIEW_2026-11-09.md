# Crop adjustment and follow-up review — 2026-11-09

## Scope

Baseline: `98ee985` (`Preserve navigation context after deletion`). Version stays
0.5.4 with Unreleased changes. This pass adds adjustable crop frames and checks
progress-window races, temporary recognition storage, APNG validation and discard
defaults. No profile migration, extension ABI change, commit or push is required.

## Crop frames

Drag any corner or side of an existing selection. Free selections adjust each
axis independently; preset and custom ratios retain their proportions within
integer-pixel rounding. A corner keeps the opposite corner fixed; a side keeps
the opposite side and perpendicular centre fixed. Image boundaries constrain the
result, including reverse drags and extreme custom ratios.

Eight small grips and standard resize cursors expose the interaction. Hit targets
are six logical screen pixels at every zoom. Very small frames retain an interior
for the existing selection magnifier. Ctrl-click clearing, right-drag moving,
background-origin selection and fullscreen navigation retain their priorities.
Dragging a grip does not navigate or zoom. Geometry is a pure helper; no image
copy, per-grip widget or cache invalidation is needed. Repainting covers only the
old and new borders, grips and selection information.

Failed trash transactions previously restored the image without its crop frame.
They now restore the exact selection and ratio along with the existing zoom.
Two regression cases modify a generated source during confirmation, verify the
stale-revision rejection, and confirm that the external file remains intact.

## Reliability corrections

- Catalog jobs no longer join by calling `QProgressDialog::exec()`. A worker could
  finish while native window creation processed events: `accept()` then ran
  before the dialog loop started, leaving the later loop waiting indefinitely.
  The existing future-based join now owns completion; fast jobs need no window.
  Closing the catalog cancels work, waits for its worker, and opens no result
  dialog. Queued progress cannot resurrect a finished window. A cancellation
  before the delayed show also keeps the progress window hidden.
- Profiles with no `navigation/askToSave` key discard edits without asking, as
  requested. Settings and runtime agree. Explicit true/false choices remain
  unchanged. When confirmation is enabled, Cancel and unsuccessful saves retain
  edits and history. Fresh-profile navigation, close and Escape tests verify the
  source file is not written.
- OCR/QR input uses a reusable RGB strip targeting at most 1 MiB and 64 rows;
  a single wider row remains the minimum. Packed rows use one write per strip;
  padded rows omit alignment bytes from PPM output. White compositing, source
  coordinates and last partial strips match whole-image reference bytes across
  twelve pixel formats. Temporary input and copied language data are removed
  when a job completes, fails or is cancelled, while recognized text remains.
- APNG CRC32 uses a 256-entry constant table and streams type/payload separately.
  It retains the checksum polynomial, corruption rejection and composition path.
  Independent bitwise fixture checks cover all byte values, corrupt payloads,
  corrupt checksums and retry after replacing the same file.

The catalog race was reproduced before the correction with an event filter that
pumps completion during Show. Its baseline needed the watchdog to exit; the
corrected fast job finishes without showing progress. Subprocess tests cover
metadata indexing, exact duplicates and similarity search with bounded failure
timeouts. They do not suppress failures or increase suite timeouts.

## Component measurements

Windows 11, GCC 16.2, Qt 6.11.2, release libraries. Each row uses three fresh
processes per variant, alternating a frozen baseline executable and current
libraries. Source generation is outside the timer; OS caches are not flushed.
Units MB are decimal. These are local component measurements, not end-to-end
opening, cold-start or recognition-engine performance guarantees.

| Component | Baseline median | Current median | Scope |
| --- | ---: | ---: | --- |
| APNG first-frame read | 405.37 ms | 161.17 ms | 1024×1024 RGBA, six frames, 25,180,099-byte deterministic noisy fixture |
| Recognition input preparation | 156.67 ms | 142.42 ms | 8000×4000 ARGB32 with transparency, 96,000,017-byte PPM |
| Recognition additional peak private commit | 96.37 MB | 1.00 MB | Process peak-commit delta around preparation; excludes the existing 128 MB source |

Recognition timing varies materially (baseline samples 139–262 ms). Earlier tile
variants were slower, so the supported conclusion is the reduced temporary
memory requirement without an observed regression in this final sample. Odd-width
inputs still need row writes and are checked for correctness, not assigned the
packed-width timing. This is not a process-wide RAM cap: source pixels, tools,
OS cache and codec allocations have separate ownership.

Local raw evidence: `build/round2-comparison.json`,
`build/round2-library-{before,after}.json`, and focused/full test logs. These are
working-tree diagnostics, excluded from source/runtime archives.

## Verification and limits

The final build passes 870 Qt cases across nine suites, both in development and
against the deployed SDK-free runtime, plus 47 Python tests. No failures or
skipped cases; RAW and recognition assets are required. Native Windows 11 runs
pass 80 cases each at scale factors 1, 1.5 and 2, with no failures or skips.
Crop tests cover all eight grips
with free, landscape and portrait ratios at three zoom levels, bounded geometry,
small selections, fullscreen isolation, crop application and Undo. Reference
bytes check streamed recognition exports; real OCR/QR tests verify early
temporary cleanup before closing the result dialog.

The unattended Windows desktop does not reliably deliver a physical pointer move
from `QTest::mouseMove`. The two hover-only cursor checks therefore dispatch a
Qt mouse event to the viewport, retaining their cursor and subsequent click/zoom
assertions. A separate native probe showed an unchanged arrow after the desktop
move and the correct magnifier after widget dispatch. Native-scale checks exercise
shown Windows widgets; they do not replace manual physical-pointer testing.

Installed modules, both recognition engines, all five OCR models and relocated
portable profiles pass without SDK paths. Existing personal profile/tool files
retain their hashes across deployment and verification. Development and deployed
executables match; temporary runtime-test binaries and Qt Test are removed.
Combined evidence: `build/round2-verification.json`, with runtime steps in
`build/round2-runtime-summary.json` and validation in
`build/review-round2-final-validation-summary.json`.

Native Windows 10, Linux and macOS execution remain unverified locally. The new
code uses Qt/C++20 without platform-specific crop or checksum paths; local
Windows success does not replace their CI or visual checks. Blocking native
codec/OS operations still finish before stack-owned worker state is destroyed.
This pass does not resolve every remaining architecture or aggregate-memory
item from the larger stabilization plan.
