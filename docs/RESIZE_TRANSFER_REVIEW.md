# Gamma byte output and nearest-neighbor follow-up

Source baseline: 82a404c. Local measurements use Windows 11, GCC 16.2,
Qt 6.11.2 and the Release build. Version remains 0.5.4; changes are Unreleased.

## Changes

For normalized 8-bit gamma output, locate the final sRGB byte using inverse
byte boundaries, with a 4096-entry guess table. This removes ordinary per-channel
power calls without interpolating output colors. Within 1e-10 of a boundary,
the original continuous transfer, 16-bit rounding and Qt byte conversion decide
the result. Sixteen-bit output and the compact-row fallback retain the original
continuous transfer. The immutable 6,136-byte object is lazy and accounted for in
gamma resize estimates. Non-gamma startup and resizes do not initialize it.

RAM-backed byte-addressable nearest-neighbor outputs of at least 8 MiB now share the
existing resize thread allowance. Workers take neighboring blocks of 256 rows
and reuse repeated source rows within their own block. They share only the
read-only column map and write disjoint raw output rows. There is no additional
pixel buffer. Packed monochrome and mapped output remain serial. Pre-cancelled nearest requests
throw before allocating output, preserving the existing exception contract.

A common worker runner owns joining and exception propagation for nearest and
filtered resizes. It joins on every exit, including thread creation failure.
The calling worker participates; the process-wide allowance still caps extra
resize threads at three. No runtime dependency or startup thread is added.

A fused SSE2 vertical-filter prototype was discarded: its full hashes matched,
but alternating measurements did not show a consistent benefit. The retained
non-gamma filter arithmetic and row cache are unchanged.

## Focused measurements

Each cell is the median of six resize calls, collected in two processes per
version, ordered before / after / after / before. The same profiler and generated
source patterns are used in both binaries. Hashing, display and painting are
outside the resize timer; focused mode hashes the final output of each process.
Both controls use the extended profiler from this change with the baseline core;
to reproduce the controls, retain this `tests/bench_ixview.cpp` while building
the baseline processing code. The baseline commit alone has no case selector.
Gamma and nearest focused groups were measured separately. This is a local
throughput measurement, with substantial host drift and no CPU isolation; it
is not an IrfanView comparison or an application-open/save benchmark.

Nearest, 1920 x 1024 to 10000 x 5333, gamma/sharpening off:

| Source | Before, ms | After, ms | Ratio |
| --- | ---: | ---: | ---: |
| RGB32 | 68.49 | 32.84 | 2.09x |
| ARGB32 | 81.22 | 31.89 | 2.55x |

Gamma on, 640 x 341 to 2048 x 1092, sharpening off:

| Source | Filter | Before, ms | After, ms | Ratio |
| --- | --- | ---: | ---: | ---: |
| RGB32 | Hermite | 101.99 | 46.34 | 2.20x |
| RGB32 | Triangle | 141.06 | 56.59 | 2.49x |
| RGB32 | Lanczos | 222.87 | 100.15 | 2.23x |
| ARGB32 | Hermite | 181.47 | 87.01 | 2.09x |
| ARGB32 | Triangle | 183.83 | 79.85 | 2.30x |
| ARGB32 | Lanczos | 298.97 | 157.78 | 1.89x |

The unchanged RGBA64 gamma Lanczos control measured 403.12 vs 387.46 ms;
that difference is not treated as an optimization. These gamma results do not
imply a gain for default non-gamma filtered resizing or for 16-bit output.
All focused before/after full-image hashes match. Evidence is recorded under
`build/resize3-{gamma,nearest}-focus-*.json`; the unsuccessful SIMD trial is
recorded separately under `build/resize3-focus-*.json`.

## Large-output guard

The initial parallel-nearest prototype also wrote mapped outputs concurrently.
Two real proportional 1920 x 1024 to 100000 x 53333 ARGB32 probes exposed a
regression. Each output is 21,333,200,000 bytes, with the operation workspace set
to 512 MiB and output backed by a reserved temporary file.

| Probe | Serial baseline | Parallel mapped trial |
| --- | ---: | ---: |
| Resize timer, seconds | 91.649 | 112.010 |
| Complete process, seconds | 122.270 | 143.080 |
| Aggregate CPU, seconds | 36.203 | 68.906 |
| Observed peak working set, bytes | 9,641,086,976 | 12,857,778,176 |
| Observed peak private usage, bytes | 87,982,080 | 88,154,112 |

These are one sequential probe per version, not a statistically isolated disk
benchmark. They do not establish a general throughput ratio, but the measured
slowdown and increased page pressure do not justify concurrent mapped copies.
The final implementation therefore preserves one writer for mapped nearest
output. No 100k nearest speedup is claimed. Both trial probes passed 57 reference
points, including tile-adjacent coordinates and the last pixel, released their
temporary files, and returned mapped accounting to zero. They are not complete
byte comparisons of the 5.3-billion-pixel output. Evidence:
`build/resize3-large-nearest-100000-{before,after}.json` and
`build/resize3-locality-summary.json`.

The complete 49-case before/after matrix also has identical hashes for all
outputs, with three repeated hashes checked per case. It includes all seven
methods, upscaling, quality downscaling, gamma, alpha and RGBA64. Those matrix
measurements precede the final mapped-output guard; image storage is disabled
in that profiler, so its allocation and processing paths are unchanged by the
guard. See `build/resize3-matrix-{before,after}.json`.

The final mapped-output policy also passes a proportional 50000 x 26667 nearest
probe with 5,333,400,000 pixel bytes and a 512 MiB operation workspace setting.
Its resize timer is 12.366 seconds; the complete process takes 16.425 seconds.
All 57 reference points pass, the backing file is removed and mapped accounting
returns to zero. Observed peak working set is 5,362,790,400 bytes; private usage
is 56,471,552 bytes. There is no adjacent 50k baseline for a speedup claim.
Evidence: `build/resize3-large-nearest-50000-final.json` and
`build/resize3-final-large-summary.json`.

## Verification

All 664 Qt cases in nine development suites and all 46 Python tests pass, without
failures or skips, with RAW/tool assets required. The new transfer test checks
every byte boundary on both sides, neighboring doubles, all 65,536 evenly spaced
16-bit input levels, 100,000 deterministic random linear values, clamping and
non-finite values against the original forward conversion. Existing independent
full-image comparisons cover gamma and all six filtered methods under parallel
load, alpha ringing and 16-bit output.

Nearest reference coverage now includes 8.4 MP outputs in twelve native formats,
including palettes, packed mono, 24-/64-bit and 128-bit floating-point pixels.
Concurrent nearest calls, cancellation, metadata, source cache keys and mapped
wide-output lifetime are also checked. The final allocation guard is covered by
the final development build; the matrix hashes above use the identical resident
path before that guard was added. See
`build/review-resize3-final-validation-summary.json`.

The same 664 Qt cases pass SDK-free in the portable Windows runtime. Installed
modules, bundled OCR/QR engines, all five OCR language models, tool relocation
and relocated-profile startup pass as well. Targeted native Windows 11 resize
coupling, fitted-wheel navigation, mapped viewport painting and original-pixel
Undo pass at 100%, 125%, 150% and 200% scaling. These are targeted native checks,
not every GUI test. Evidence: `build/resize3-runtime-summary.json` and its
portable/native result logs.

## Reproduction

```powershell
# Full 49-case matrix: three output hashes per case, checked for determinism.
./build/ixview_benchmarks.exe build/resize-matrix.json --profile-resize-matrix -platform offscreen
# Focused case: three timing calls, last output hashed; zero-based matrix index.
./build/ixview_benchmarks.exe build/gamma-lanczos.json --profile-resize-matrix --resize-case=19 -platform offscreen
# Opt-in proportional probe writes about 21 GB and requires twice that free space.
./build/ixview_benchmarks.exe build/nearest-100k.json --probe-resize-axis=100000 --resize-filter=6 -platform offscreen
```

See [the previous scale review](RESIZE_SCALE_REVIEW.md) for filter tiling,
large-image storage limits and earlier measurements. File-backed output is not
a strict RSS cap; encoding and final unmapping/deletion can add considerable
time. Native Linux, macOS and Windows 10 execution are unavailable locally.
