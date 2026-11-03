# Superseded image-open work — 2026-11-03

Follow-up: [native standard sharpening](NATIVE_SHARPEN_2026-11-03.md) records the
next measured filter improvement and the newer complete regression totals.

## Problem and behavior

A newer explicit open previously waited while an older request decoded,
converted its color profile, sharpened, installed the intermediate document,
updated history/settings and indexed its folder. Only afterward did the newer
request start, through a 50 ms deferred-open polling timer.

For safe replacement cases, each image-open worker now owns a shared cancellation
token. A different newer explicit request marks it superseded. A queued load can
skip reading entirely; a native decode already in progress can finish, but its
obsolete presentation work and document installation are skipped. Completed,
unchanged originals may still enter the normal bounded cache for later reuse.

Cancellation is represented by an empty optional result, distinct from a failed
read. Unreadable files still return normal errors with their filenames. The
ordinary non-cancellable load/cache interfaces remain available as wrappers.
Cancellation checks occur before reading, after pre-decode/RAW-preview work,
after decoding and around display preparation. Native decoders and an already
running display conversion are not made interruptible.

The GUI applies this only to full-load work over an unmodified, nonanimated
current document with no RAW preview already displayed. Queued RAW callbacks
also check the token before touching the view. This avoids mixing a superseded
preview with the retained document. Visible RAW previews, animation/page work,
dirty documents, edits, Undo/Redo, file operations and saving retain their existing
flow. Repeated arrow navigation still accumulates relative steps and completes
the intermediate load needed to resolve them.

The token also reaches the existing RAW-preview producer's own cancellation
boundaries. A background hotfolder snapshot cannot overtake a deferred manual
open, including the interval after a modal dialog closes but before delivery.
The old implementation reproducibly selected the automatic `2.png` instead of
the explicitly chosen `1.png`; the corrected implementation preserves `1.png`.
Standalone before/after records: `build/priority-proof-before.json` and
`build/priority-proof-after.json`; the same scenario is a workflow regression.

Cancelled results do not clear history/selection/zoom, replace the visible image,
write recent-file entries or trigger unreadable-file handling. If the newest
explicit target fails, the previously visible document remains. The newest target
is the one named by the error dialog.

`deliverDeferredOpen` owns deferred delivery for both the existing polling timer
and a queued call after worker completion. It rechecks busy/modal state and stops
the polling timer on successful delivery. Its timer interval remains 50 ms;
completion does not turn it into a zero-interval busy loop. Window destruction
marks queued load work cancelled and still joins owned workers before teardown.

## Verification and measurement method

New regressions cover cancellation before read and after pre-decode work,
distinguishing cancellation from errors, skipping presentation work, retaining
only a bounded completed original, newest-open priority, failed replacement
preserving image/zoom/selection, dirty-document behavior and queued arrow steps.
Existing save, modal, animation, unreadable-file and shutdown tests remain enabled.

```powershell
./build/ixview_benchmarks.exe build/supersede.json --profile-superseded-open C:/images/large.png C:/images/latest.jpg -platform windows
```

The native Windows profiler creates a shown 1200 x 800 window with an isolated
profile. A worker gate queues the first open. A newer explicit open arrives before
the gate is released. Timing runs from that newer request to document installation
and Qt paint completion. The stale fixture is a 6000 x 4000 PNG, the latest fixture
a 1920 x 1280 JPEG. Caches use 192/16 MiB, previews are hidden and OS caches warm.
This deliberately measures replacement of a queued load, not ordinary first-open
or keyboard-folder-navigation speed. Hashes and a cache probe run outside timers.

Before/after use frozen libraries and the same profiler source, compiled against
the matching MainWindow headers because its private object layout changed:

- core: `0d9d2a9b125b1a9c523928587583aa2c8f8c50e6fee69008416390dbc0c2408e`
- UI: `be41cb5ad5a1fa4abc81c0172a87f7c66eeb2ca244a58730f38cfae926fb102c`

Five fresh-process pairs alternate execution order. All latest-image original
and display hashes must match. A post-timer cache miss for the stale file shows
whether its queued decode was skipped; the profiler records whether it was
installed at all. Raw results: `build/supersede-comparison.json` and
`build/supersede-summary.json`.

The verification probe may decode the skipped file after timing; that probe is
not part of the production replacement flow. Frozen local archives/header copies
are ignored measurement artifacts, not additional shipped implementations.

## Native Windows comparison

Windows 11, Intel Core i5-10210U, approximately 16 GiB RAM, GCC 16.2 Release and
Qt 6.11.2. Five fresh-process pairs with alternating order; CPU frequency and
background load were not controlled. Values are medians:

| Controlled queued-load replacement | Before | After |
| --- | ---: | ---: |
| Newest request to document installation | 1003.49 ms | 150.24 ms |
| Newest request to first Qt paint | 1057.93 ms | 226.41 ms |
| Retained cache charge before verification probe | 105.83 MB | 9.83 MB |

The first Qt paint is roughly 4.7 times faster in this controlled scenario.
Before samples span 1053–1129 ms; after samples 204–281 ms. Every before run
installs the obsolete PNG and retains its decoded pixels. Every after run skips
that installation and the post-timer probe misses the stale original, proving
that its queued decode did not run. Latest-image original/display hashes match
in every before/after sample.

This final comparison uses the fully validated binary including the Hotfolder
guard and RAW token propagation. The earlier five-pair comparison (paint
1219.79 / 226.20 ms) remains separate in `build/initial-supersede-*.json`;
it is not pooled with these final samples.

These results do not measure a faster PNG/JPEG decoder or a general first-open
gain. They isolate a newer explicit request overtaking still-queued work. For a
native read already running, only later stages can be avoided; the request still
waits for that decoder to finish. Retained cache charge is not process RSS/private
commit. The lower charge here follows from never decoding the obsolete file,
within the same configured limits.

These stage boundaries do not provide preemptive cancellation, a process RAM cap,
or full coordination of all loading work. Native Windows 10/Linux/macOS validation
and a broader loading/navigation state-machine extraction remain open.

## Complete validation

The final Release build passes **709 Qt cases in nine suites and 47 Python
tests** with required RAW/recognition assets, no failures and no skips.
The maintenance suite has 47 cases; workflows have 165. The focused cancellation
checks are recorded in `build/supersede-focused.txt`; complete results are in
`build/review-superseded-load-final-validation-summary.json`.

The deployed Windows runtime passes the same **709 Qt cases** without SDK
directories in PATH, with no failures/skips. Real OCR/QR engines, all five OCR
models, relocated tools/profile startup and installed optional-module loading
also pass. The deployed executable matches the validated development executable
byte for byte. Records: `build/superseded-load-runtime-summary.json` and
`build/superseded-load-portable.log`.
