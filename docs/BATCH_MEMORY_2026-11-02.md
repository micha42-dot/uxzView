# Batch memory admission — 2026-11-02

## Problem and implementation

Previously each pipeline run could launch four decodes/transforms whose individual
limits did not account for their combined live work. Multiple runs compounded
that pressure. Small workloads still benefit from parallel execution.

`core/batch_memory.*` adds a weighted FIFO gate shared by pipeline runs. It uses
the existing immutable image-memory budget; no new setting or startup thread is
introduced. Header dimensions/format and the complete chain estimate peak work,
including resize scratch, export/conversion buffers, a 16 MiB allowance and
conservatively charged shared watermark pixels. Unknown headers/configured codecs
consume the whole capacity. Oversized jobs run alone and retain existing mapped
processing rather than being rejected by this scheduler.

A move-only lease is acquired before decoding and released after image/export
buffers, including exceptions. Queued work checks cancellation every 20 ms.
FIFO ordering prevents a stream of small jobs overtaking a waiting large job.
Sources are checked again after waiting; changed files cannot reach the decoder
under their old authorization. Existing final revision and atomic-commit guards
remain. The configured worker count is an upper bound, not a promise to start
that many memory-heavy files simultaneously.

Maintainer contract: cancellation belongs to one run and remains set until that
run has joined; do not reuse/reset its flag while workers are still active.
Adding a processing step requires updating validation, transformation and the
memory estimate together. Header queries must remain separate from decoding.

This is **estimated batch admission, not a process RAM/RSS cap**. Caches, active
documents, history, resource loading before job admission, decoder-private
allocations and mapped-page residency are not fully coordinated. Plugin header
queries may themselves allocate and precede admission. Estimates may be
conservative or unknown. A configured ImageCodec has no dimension-query
contract, so those jobs deliberately serialize. Normal image browsing is unchanged.

## Reproducible comparison

`ixview_batch_memory_benchmarks` processes four generated identical RGBA64 PNGs
using Lanczos and PNG export, with transparent pixels and a Display P3 profile.
The small scenario is 64 x 48 to 96 x 72; the large scenario is 3000 x 2000 to
5000 x 3333. Inputs are warm from fixture generation. Fixture creation, planning,
decoding/verification of the result and hashing are outside the timer.

```powershell
cmake --build build --target ixview_batch_memory_benchmarks --parallel 4
./build/ixview_batch_memory_benchmarks.exe build/batch-small.json small 512
./build/ixview_batch_memory_benchmarks.exe build/batch-large.json large 512
./build/ixview_batch_memory_benchmarks.exe build/batch-large-roomy.json large 8192
```

The last argument is the image-memory budget in MiB. Windows private bytes are
sampled every 5 ms during processing; other platforms report zero for that metric.
This is sampled committed memory, not peak RSS, live-buffer accounting or an exact
allocator measurement. Short spikes can be missed. Wall times are local samples,
not general speedup claims.

The before executable links the frozen pre-change core library
(`70ed7ffea335623772a41e95c03a4232c0efbde6d00b220b62905af9cb36bf98` SHA-256),
the after executable the current core, using the same profiler object and link
options. The frozen library is an ignored local measurement artifact, not a second
shipped implementation. Runs alternate before/after order in fresh processes.
Every decoded output must retain size, 64-bit pixels and Display P3; before/after
pixel hashes must match. No profiling-only bypass is added to production code.
The measurements precede the final estimator correction for grayscale/RGB30
normalization and floating-point watermark chains. Those formats are not used
by this profiler: the measured RGBA64 chain's estimates/admission are unchanged.

Environment: Windows 11 Pro 10.0.26300, Intel Core i5-10210U (4 cores/8 threads),
about 16 GiB installed RAM, GCC 16.2 Release and Qt 6.11.2. No CPU/frequency
isolation was applied. Values below are medians across fresh-process runs;
large scenarios have only two samples per implementation, so their timings
are descriptive comparisons, not stable throughput distributions.

| Scenario / budget | Runs per version | Before time | After time | Before sampled private peak | After sampled private peak |
| --- | ---: | ---: | ---: | ---: | ---: |
| Small / 512 MiB | 10 | 22.07 ms | 23.29 ms | 42.46 MiB | 42.46 MiB |
| Large / 512 MiB | 2 | 8.31 s | 13.37 s | 1450.99 MiB | 681.73 MiB |
| Large / 8192 MiB | 2 | 8.98 s | 8.62 s | 1451.31 MiB | 1451.37 MiB |

With the deliberately tight 512 MiB budget, sampled committed memory falls
about 53%, while elapsed time increases about 61%. This is a memory/throughput
tradeoff, not a speedup. Even the reduced process peak exceeds the image budget:
the estimate is not an RSS/commit ceiling. With 8 GiB, these jobs fit concurrently;
the sampled memory peak stays essentially unchanged and timings remain in the
same range. The small scenario adds about 1.2 ms in this sample. No general
startup, navigation or batch-speed improvement is claimed. All 112 decoded
outputs across the 28 process runs retain their size, depth, profile and identical
before/after pixel hashes. Raw data: `build/batch-memory-comparison.json`.

## Verification scope

Regression coverage includes estimate bounds for 8/16/32-bit samples, very wide
geometry, aggregate concurrent reservations, moved lease ownership, exception
cleanup, queued cancellation, simultaneous runs and an external rewrite while
queued. The integration checks prove that canceled/changed queued inputs never
reach the configured decoder or create outputs.
Additional estimate assertions guard 16-bit grayscale/RGB30 expanding to RGBA64
and preservation of 128-bit floating-point estimates after watermarking.

Native Linux, Intel macOS and Apple Silicon CI confirmation remains required.
This block adds no new codec, DLL layout or complete MainWindow extraction.

The Release build and all **681 Qt cases in nine suites plus 47 Python tests**
pass locally with RAW/recognition assets required, without failures or skips.
Results: `build/review-batch-memory-final-validation-summary.json`.

The final deployed Windows runtime also passes all nine suites (681 cases, no
failures/skips) without the SDK in PATH. Bundled QR/OCR engines, five OCR models,
relocated tools/profile startup and installed optional modules pass. Development
and deployed executables match byte for byte. Results:
`build/batch-memory-runtime-summary.json` and `build/batch-memory-portable.log`.

Follow-up: [cache coordination and navigation](EVERYDAY_PERFORMANCE_2026-11-02.md)
adds an atomic retained-cache observer to admission. New jobs account for cache
ownership; active work is not preempted and a sole oversized job may still run.
