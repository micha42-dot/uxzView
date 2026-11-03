# Decode reuse between thumbnails and foreground opening — 2026-11-03

Follow-up: [overlapping active decodes](CONCURRENT_DECODE_2026-11-03.md) can now
share native pixel work before a thumbnail has completed. The measurements
and validation counts below describe this preceding completed-preview block.

## Scope

Native PNG/BMP readers normally decode full pixels even when a small thumbnail
is requested. Previously those pixels were discarded and decoded again when
opening that neighbor. The preview worker can now retain that already-required
decode in spare image-cache capacity. This does not start another decode,
parse metadata in thumbnail workers or change the original image.

Eligibility is deliberately narrow: unchanged static, single-page, unrotated
native PNG/BMP, without decoder-side scaling, and a decoded buffer between
1 and 96 MiB. APNG, JPEG, RAW, PDF and transformed images keep their existing
paths. Both image and thumbnail caching must be enabled. The foreground reader
still probes the actual format, validates dimensions and memory policy, reads
metadata and constructs the full document. Only its pixel decode is skipped.

Source revisions are checked after thumbnail scaling and before foreground
handoff; size, pixel format and codec must match. Clear, targeted invalidation,
source changes and configuration epochs prevent stale insertion/reuse. Retained
pixels are consumed once, then the regular original-document cache takes over.

The additional entries share the existing image-cache allowance with originals
and prepared presentations. There is no independent memory budget. Foreground
originals take priority. Nearest-neighbor requests are submitted next/previous
before farther neighbors; speculative retention keeps this priority when workers
finish out of order. The visible thumbnail order is unchanged. Minimum entry
size and existing cache ceilings bound the priority bookkeeping.

Cache ownership remains an estimate rather than a process-RAM limit. Active
decoders, document/history references and viewport/animation caches can hold
other buffers. This work does not complete global memory coordination.

## First-open investigation and rejected experiment

Opt-in `IX_PROFILE_IMAGE_LOAD=1` diagnostics split native codec work into header,
pixels, properties and metadata. Windows console capture additionally needs
`QT_FORCE_STDERR_LOGGING=1`. Ordinary operation emits no stage messages.

For local generated 6000 x 4000 files, PNG pixel decoding took roughly
305–352 ms and metadata 19–21 ms; JPEG pixels were around 342 ms. Header probing
was below 1 ms. These are warm-filesystem examples, not cold-disk guarantees.

An experimental owned input buffer was rejected. Six alternating file/buffer
pairs included the bulk read in the timer and validated decoded pixels, DPI,
profiles and text keys outside it:

| Fixture | QFile median | QBuffer including input read | Process read calls, file / buffer |
| --- | ---: | ---: | ---: |
| 24 MP JPEG, 23.2 MB | 358.63 ms | 410.38 ms | 1417 / 2 |
| 24 MP PNG, 72.1 MB | 305.63 ms | 310.75 ms | 4403 / 4 |
| 2.4 MP JPEG, 4.5 MB | 58.40 ms | 57.34 ms | 275 / 2 |

Fewer read calls did not establish a useful decode speedup and required another
whole compressed-file buffer. Production decoding therefore retains its file
device. `--profile-reader-input` exists only in the benchmark executable.
Raw measurements: `build/reader-input-*.json`.

## Reproduction

```powershell
cmake --build build --target ixview_benchmarks --parallel 4
./build/ixview_benchmarks.exe build/handoff.json --profile-preview-handoff C:/images/large.png -platform windows
```

The profiler creates an isolated profile and a shown 1200 x 800 window before
timing. It measures one real 68 x 50 thumbnail, then the first foreground open
of that same file, reporting preview, foreground and combined times separately.
It uses a 1 GiB image budget, 192 MiB image cache and 16 MiB thumbnail ceiling.
Source/display/thumbnail SHA-256 and metadata comparisons are outside the timer.
Qt paint events are observed, not compositor presentation. OS caches are warm.

Before/after reuse the same benchmark object with frozen pre-change archives:
core `bdf1f8b20f327d0ef2d16afbe2273469f2e3f18cd1d3dd0055c59e74f4e8a1cb`,
UI `f9a89bc83b3dc8f9a2661d06a807459fbb0cfdc0fe0edebb4979fb31bfa69091`.
The baseline already contains the earlier presentation-cache improvements.
Three fresh-process pairs alternate execution order. The JPEG control must
retain its reduced-decode path. Raw results: `build/native-handoff-comparison.json`
and `build/native-handoff-summary.json`.

## Native Windows comparison

Windows 11, Intel Core i5-10210U, approximately 16 GiB RAM, GCC 16.2 Release,
Qt 6.11.2; three fresh-process samples per side with alternating pair order.
CPU frequency and unrelated background load were not locked. Values are medians:

| 6000 x 4000 fixture | Preview before / after | Foreground paint before / after | Combined before / after |
| --- | ---: | ---: | ---: |
| PNG, 72.1 MB file | 326.56 / 318.51 ms | 334.92 / 67.01 ms | 658.97 / 385.52 ms |
| JPEG, 23.2 MB control | 204.99 / 196.88 ms | 382.71 / 382.57 ms | 584.15 / 555.20 ms |
| BMP, 72.0 MB file, separate three-pair run | 221.15 / 265.60 ms | 354.42 / 62.04 ms | 575.57 / 338.18 ms |

For PNG, document installation fell from 309.90 to 22.32 ms. Foreground latency
was about five times lower, combined work about 41% lower. The preview did not
become slower to move work out of the foreground timer. Original, display and
thumbnail hashes, DPI, format and metadata tags match in every comparison.
The JPEG control retains only the roughly 12.8 kB thumbnail charge; its decoding
path is unchanged and these noisy samples establish no JPEG speedup.

BMP also avoids its second decode, but its desktop measurements were especially
noisy: baseline paint samples ranged from 215 to 618 ms; preview medians grew
by about 44 ms. The combined median still fell by about 41%. Its separate raw
results are `build/native-handoff-bmp-comparison.json` and
`build/native-handoff-bmp-summary.json`. Do not generalize these small samples
into a fixed latency guarantee or a faster thumbnail-decoding claim.

Retained cache ownership before opening PNG increases from approximately 12.8 kB
to 96.0 MB because its already-decoded 24 MP RGB32 pixels are now reusable.
This retention is within the existing image allowance; it is not a memory-use
reduction claim. The next foreground read consumes these pixels and admits the
regular original instead of keeping two independent full buffers.

An untouched first image without a completed eligible thumbnail still requires
its full decode. No general first-open, startup or native Unix speedup is claimed.

## Verification

The Release build passes **692 Qt cases across nine suites and 47 Python tests**
with required RAW/recognition assets, no failures and no skips:
`build/review-preview-handoff-final-validation-summary.json`.

New regressions compare native RGB/16-bit/alpha PNG and BMP previews against
the old reader/scaling path and foreground documents against ordinary decoding.
They verify exact pixels, profile, text, DPI, metadata, source preservation,
one-shot handoff, priority under constrained budgets, original preemption,
targeted invalidation, external rewrites, mismatched size/codec, disabled caches,
cancellation and JPEG/APNG exclusion. Existing clear/in-flight, navigation,
rendering, file-operation, batch and format regressions remain enabled.

The deployed runtime also passes all **692 Qt cases** without the SDK in PATH,
with no failures/skips. Real QR/OCR engines, all five OCR models, relocated
tools/profile startup and all installed optional modules pass. The deployed
executable matches the validated build byte for byte. Records:
`build/preview-handoff-runtime-summary.json`, `build/preview-handoff-portable.log`.

Native Windows 10, Linux and macOS confirmation and coordination of active
documents/Undo/viewport allocations remain open. No new dependency or SIMD
requirement was introduced by this change.
