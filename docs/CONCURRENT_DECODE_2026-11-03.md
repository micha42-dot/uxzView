# Sharing overlapping native pixel decodes — 2026-11-03

Follow-up: [superseded open requests](SUPERSEDED_LOADS_2026-11-03.md) add stage
cancellation and manual-open priority. Counts and measurements below describe
this earlier concurrent-decode block.

## Problem and implementation

The [completed-preview handoff](PREVIEW_HANDOFF_2026-11-03.md) avoids decoding
again after a thumbnail is ready. It does not help if the user opens that file
while its thumbnail worker is still decoding: both readers could allocate and
decode the same full image concurrently.

Eligible simultaneous native PNG/BMP full-pixel reads now share one active
decode. The first worker owns its reader; matching workers wait for its pixels.
Each foreground reader still owns its header checks, metadata, file properties,
revision validation and document construction. Only unrotated, static,
single-page native buffers between 1 and 96 MiB are eligible. JPEG, APNG, RAW,
PDF, decoder-scaled and disk-backed reads keep their existing paths.

Sharing uses lexical absolute path, source revision, dimensions, pixel format,
codec and cache generation. A completed, unchanged, matching buffer is handed
over through QImage implicit sharing. Failed or rewritten results are not shared;
waiters retry with their own reader so its error diagnostics remain correct.
Exceptions wake all waiters. Clear/invalidation/configuration separate new
generations; an older completion cannot remove the newer owner's entry.

No cache mutex is held while decoding or checking the source revision. A
condition wait releases it; optional preview cancellation is checked every
20 ms while waiting. Cancelling a waiter leaves the owner's work usable by
other readers. Reentrant use never waits on the same thread. There is no new
thread pool, waiting timer on the GUI thread or persistent result cache.

When foreground work joins a low-priority preview decode, its Qt worker is
temporarily raised to normal priority and restored on completion, including
failure. Actual OS scheduling is platform-dependent. Activity snapshots count
coordinated owners/waiters; they are diagnostics, not a complete decoder count
or process-memory limit. Once participants leave, their active record disappears.

## Verification and measurement method

Controlled semaphore-driven tests verify a single decode/shared buffer,
cancellable waits, independent generations, source rewrites, null results,
exceptions, reentrancy and Windows worker-priority restoration. They synchronize
on active/waiting state rather than relying on scheduling sleeps. Existing
pixel/metadata, cache, navigation, file-safety and large-image tests remain enabled.

```powershell
cmake --build build --target ixview_benchmarks --parallel 4
./build/ixview_benchmarks.exe build/concurrent.json --profile-concurrent-preview C:/images/large.png -platform offscreen
./build/ixview_benchmarks.exe build/single.json --profile-concurrent-preview C:/images/large.png --uncontended -platform offscreen
```

The benchmark releases an actual 68 x 50 thumbnail and foreground `loadImage`
through a barrier. It measures worker completion and total wall time, process
CPU time and private committed-memory growth sampled every 2 ms on Windows.
`--uncontended` loads only the foreground image. Unsupported native counters
are null. Thread setup and output hashing are outside the timer. No GUI painting
or cold startup is measured. Five fresh-process pairs alternate before/after
order; the filesystem cache is warm. Foreground, display and thumbnail hashes
must match in every run.

Before/after use the same benchmark object with frozen pre-change archives:
core `3bab1c11147cbe55439b0bbcb94085d2bd8c0f5351fc85166c532ade052647b2`,
UI `be41cb5ad5a1fa4abc81c0172a87f7c66eeb2ca244a58730f38cfae926fb102c`.
This baseline already includes completed-preview handoff and presentation reuse.
Raw measurements: `build/inflight-comparison.json`, `build/inflight-summary.json`.

## Local Windows measurements

Windows 11, Intel Core i5-10210U, approximately 16 GiB RAM, GCC 16.2 Release and
Qt 6.11.2. All three files are 6000 x 4000 pixels. CPU frequency and unrelated
background activity were not controlled. Five samples per side; medians:

| Overlapping thumbnail + foreground | Foreground before / after | Process CPU before / after | Private-memory growth before / after |
| --- | ---: | ---: | ---: |
| PNG, 72.1 MB file | 528.82 / 506.26 ms | 953.13 / 578.13 ms | 193.16 / 96.83 MB |
| BMP, 72.0 MB file | 237.55 / 222.15 ms | 500.00 / 250.00 ms | 192.70 / 96.53 MB |
| JPEG, unchanged control | 470.33 / 536.94 ms | 734.38 / 906.25 ms | 98.33 / 98.19 MB |

PNG process CPU time fell by about 39%, BMP by 50%, and observed private-memory
growth roughly halved. Foreground latency gains are modest in this sample;
there is no fivefold latency claim for overlapping decodes. This differs from
opening after an already-completed preview, measured in the preceding review.
The extra PNG/BMP full pixel buffer disappears in every measured after case.
Original/display/thumbnail hashes match before/after throughout.

The JPEG control was slower in the first set. An uncontended PNG control was
also slightly slower: foreground 481.79 / 494.24 ms, CPU 484.38 / 500.00 ms.
A separate six-pair control-only repeat changed those directions/magnitudes:
JPEG foreground **938.91 / 943.16 ms**, CPU **1531.25 / 1460.94 ms**;
uncontended PNG foreground **1042.75 / 971.97 ms**, CPU **1054.69 / 968.75 ms**.
Within that run, both implementations drifted from roughly 350–500 ms into
approximately one-second loads. The cause of this system-performance change
was not established; do not assume fixed CPU frequency or derive a general
JPEG/uncontended speedup or slowdown. Raw repeat data:
`build/inflight-control-comparison.json`, `build/inflight-control-summary.json`.

The reliable conclusion is removal of one simultaneous full PNG/BMP pixel
decode and buffer, backed by deterministic sharing tests and memory samples.
These measurements do not establish responsiveness under every background load,
first-paint latency, cold startup, or improvements for unique uncached files.

The active entry ends when its pixel owner finishes. A request starting later,
before regular thumbnail/document admission finishes, can still miss both the
active entry and the cache. Coalescing all loading stages/formats is future work.

Active documents, Undo, viewport allocations, decoder-private working buffers and
all other active work are not covered by a single global RAM reservation scheme.
Already-running native decoders are not made interruptible. Unique first opens
still require decoding, and native Linux/macOS/Windows 10 confirmation remains open.

## Complete validation

The Release build passes **701 Qt cases in nine suites and 47 Python tests**,
with required RAW/recognition assets, no failures and no skips.
`build/review-concurrent-decode-final-validation-summary.json` records the run.
The maintenance suite includes 40 passing cases; the focused concurrency run
is recorded in `build/inflight-focused.txt`.

The deployed runtime passes the same **701 Qt cases** without SDK directories
in PATH, with no failures/skips. Real OCR/QR engines, all five OCR models,
relocated tool/profile startup and installed optional-module loading pass.
The deployed executable matches the validated development executable byte for
byte. Records: `build/concurrent-decode-runtime-summary.json` and
`build/concurrent-decode-portable.log`.
