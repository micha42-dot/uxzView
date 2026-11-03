# Cache coordination and everyday navigation — 2026-11-02

Implementation and initial measurements: 2026-11-02. Final measurements/review:
2026-11-03 (Europe/Berlin). Baseline source state: `298bae9`.

Follow-up: [reuse of already-decoded neighbor pixels](PREVIEW_HANDOFF_2026-11-03.md)
adds a fourth cache kind within the same limits. Counts and measurements below
describe this earlier presentation-cache block.

## Implemented changes

The previous original cache avoided decoding on a repeat visit but still
converted color profiles and applied display sharpening again. These are worker
operations, yet users waited for them before the next image could be installed.

`ImageLoadCache` now retains one prepared presentation for an unchanged cached
original, identified by revision/page, original QImage identity and the two
presentation flags. Original precision/profile are unchanged. Work happens
outside the cache lock; cache identity/epoch are checked again before insertion.
Ordinary sRGB/unmanaged aliases do not need another cached pixel buffer.
They also bypass the presentation-cache lookup altogether. The decision shares
the same color-conversion predicate as the actual conversion path.

The new presentation cache has **no independent memory allowance**: it uses only
spare capacity within the existing original-image budget. Incoming originals
reclaim presentations first, preserving the original cache's LRU and avoiding
re-decoding just to retain converted pixels. Previews retain their requested
ceiling but yield to foreground originals under shared-budget pressure. Combined
original/presentation/preview cost cannot exceed the requested image+preview
limits or the image-memory budget. Clear, targeted invalidation, configuration
changes, source rewrites and concurrent completion retain their generation guards.

Batch admission samples an atomic retained-cache cost. Cache locks are never
acquired under the gate mutex. Waiting jobs observe changed cache cost within
the existing 20 ms cancellation/admission polling. Active jobs are not preempted;
a sole job may exceed the remaining estimated headroom, retaining existing
per-operation and mapped-storage safeguards rather than blocking indefinitely.

These are cache-ownership estimates, **not an RSS or process-commit limit**.
Active documents, Undo/Redo, viewport rasters, APNG sessions, temporary conversion
buffers, borrowed image references, decoder-private allocations and mapped-page
residency are not all coordinated. Evicting a cache reference need not free a
buffer still used by the UI or another worker. This is further coordination, not
completion of the full global-memory plan.

## Measurement method

```powershell
cmake --build build --target ixview_benchmarks --parallel 4
./build/ixview_benchmarks.exe build/navigation.json --profile-cached-navigation -platform windows
```

The profiler uses an already-running, shown 1200 x 800 native Windows window
with an isolated profile, 1 GiB image budget, 192 MiB image/presentation cache,
16 MiB preview budget and the optional preview strip hidden. It calls the normal
image-open path to alternate between two real files, recording installation,
Qt-observed paint completion and GUI timer gaps. This observes Qt paint/event
delivery, not compositor/display latency or keyboard-event handling.

Scenarios are a 1920 x 1280 sRGB JPEG control, a Display P3 JPEG of the same size
and 3000 x 2000 Display P3 RGBA64 PNGs with varying alpha. First opens of both
files are recorded separately from 20 warmed switches per scenario. Fixture
generation, SHA-256 pixel verification and window preparation are outside the
timer. OS caches are warm from fixture creation and are not flushed. Every visit
checks that original/display pixel hashes match that file's first visit.

Before/after use the same profiler objects with frozen pre-change libraries:

- core: `31fbe258c48078f861d01ddd964b99e9a4d506773db0324226c5e111914f580f`
- UI: `f9a89bc83b3dc8f9a2661d06a807459fbb0cfdc0fe0edebb4979fb31bfa69091`

Frozen archives are ignored local baseline artifacts, not duplicate shipped
implementations. Three pairs alternate before/after order in fresh processes;
each pair must produce identical original/display hashes. No profiling bypass
is introduced into production code. The profiler uses Qt's Windows widget style;
these measurements do not establish performance on Windows 10, other styles,
Linux/macOS, every ICC profile or directories containing hundreds of unique files.

Environment: Windows 11 Pro 10.0.26300, Intel Core i5-10210U (4 cores/8 threads),
about 16 GiB installed RAM, GCC 16.2 Release and Qt 6.11.2. CPU frequency,
desktop DPI/compositor scheduling and unrelated background activity were not
locked. Each side has 60 warmed switches per case across three fresh processes;
samples within a process are correlated. p95 is descriptive, not a guarantee.

| Scenario | Install median before / after | Paint median before / after | Paint p95 before / after |
| --- | ---: | ---: | ---: |
| sRGB JPEG, 1920 x 1280 | 3.84 / 4.42 ms | 28.02 / 33.13 ms | 64.82 / 68.49 ms |
| Display P3 JPEG, 1920 x 1280 | 15.66 / 4.05 ms | 39.21 / 28.72 ms | 93.72 / 67.56 ms |
| Display P3 RGBA64 PNG, 3000 x 2000 | 39.83 / 3.40 ms | 88.48 / 48.21 ms | 125.40 / 63.91 ms |

The sRGB control was slower in this set. A separate six-pair control-only repeat
(120 warmed switches per side, alternating process order, identical source and
display hashes) reversed that direction: install median **4.39 / 3.88 ms**, paint
median **34.57 / 30.14 ms**, p95 **68.75 / 53.76 ms**. There is no reproducible
general sRGB speedup or slowdown claim. The native desktop timings are noisy;
the clear target improvement is avoiding repeat color conversion in the P3 cases.

First opens are six samples per side/case, including initial decoder/conversion
work: sRGB paint medians **45.58 / 62.18 ms**, P3 JPEG **66.78 / 57.07 ms**, P3 PNG
**280.33 / 304.03 ms**. These mixed results do not establish faster initial opens
or cold startup. Warm GUI timer-gap maxima still reach roughly 67–95 ms in the
full comparison; all UI stalls have not been resolved. SHA verification is outside
the timer but runs between visits on the GUI thread, so request pacing and queued
work can affect later samples. A future latency study should keep validation off
the GUI thread and record/fix the physical viewport and desktop DPI explicitly.

All original/display hashes match between before/after builds and every repeated
visit. Converted pixels consume otherwise unused space in the existing cache;
this is CPU reuse within a bounded pool, not a claim of universally lower process
memory. Inputs larger than the existing 96 MiB original-admission ceiling and
working sets that leave no presentation capacity keep their previous conversion
path. UI rendering itself is unchanged. No new dependency is introduced.

Raw results: `build/navigation-comparison.json`, `build/navigation-summary.json`,
`build/navigation-control-comparison.json`, `build/navigation-control-summary.json`.
The earlier comparison is retained as `build/initial-navigation-*.json` and is
not pooled with the final measurements. To reproduce the control-only run:

```powershell
./build/ixview_benchmarks.exe build/navigation-control.json --profile-cached-navigation --navigation-srgb-only -platform windows
```

## Verification and remaining scope

Regressions exercise exact presentation reuse, color/sharpen policy changes,
original priority under cache pressure, late conversion after clear, targeted
invalidation, external rewrites and changing retained-cache costs during batch
admission. An isolated child initializes a real 8 MiB image budget, requests
512/64 MiB caches and checks their combined charge while original images reclaim
thumbnail capacity. No large allocation is needed for that low-budget test.

The final Release build passes **687 Qt cases in nine suites and 47 Python
tests**, with RAW/recognition assets required, no failures and no skips.
`build/review-everyday-final-validation-summary.json` records the full run.
`build/everyday-maintenance-final.txt` also records the focused 26-case check
after strengthening policy coverage with varied 16-bit pixels. The final
production executable was unchanged by the later control-only profiler flag.

The final deployed Windows runtime also passes all **687 Qt cases** without the
SDK in PATH, with no failures/skips. Bundled QR/OCR engines, all five OCR models,
relocated tools/profile startup and installed optional modules pass. The deployed
executable matches the validated development executable byte for byte. Results:
`build/everyday-runtime-summary.json`, `build/everyday-portable.log`.

Cold process startup, unique-image traversal, prefetch scheduling, zoom/pan
profiling, a combined Undo/active-document reservation scheme and native Unix
confirmation remain open. This change is primarily for repeat visits; it does
not eliminate the initial decode/color-conversion cost.

Qt cache cost/trim behavior was checked in the local 6.11.2 SDK and the
[Qt 6.8.3 implementation used by CI](https://raw.githubusercontent.com/qt/qtbase/v6.8.3/src/corelib/tools/qcache.h).
This source check is not a substitute for native Linux/macOS execution.
