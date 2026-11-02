# Resize scaling review

For the subsequent gamma-output and parallel nearest-neighbor changes, see
[the byte-transfer follow-up](RESIZE_TRANSFER_REVIEW.md).

The follow-up targets all seven resize methods and 50,000/100,000-pixel axes.
The source baseline is d51c094, after the first resize optimization and Settings
changes. Public processing APIs, filter weights and output precision remain.

## Implementation

- Nearest-neighbor uses fixed-size native pixel copies and reuses output rows
  with the same source Y. Packed monochrome and palette semantics remain.
- x86 byte output uses baseline SSE2 for clamping, rounding and BGRA packing.
  Byte ties and legacy long-overflow cases use the scalar quantizer. Other
  architectures retain the scalar path; no AVX or fast-math requirement is added.
- Gamma normalization reuses the exact transfer-function result for all 65,536
  possible RGBA64 input values. The read-only 512 KiB table is initialized on
  first use and counted in gamma resize estimates.
- When full-width normalized rows would restrict parallelism, column tiles stay
  at or below 8192 pixels. Up to four workers take adjacent output stripes of at
  most 256 rows; short images use smaller stripes. This localizes mapped writes
  instead of concurrently scattering dirty pages across multi-GB row bands.
- Cache keys are source-row indices, so changing the column tile clears that
  cache. Weights use global coordinates. The planned tile width remains fixed
  when fewer worker slots are available, keeping scratch allocations within the
  shared GUI/worker estimate. The ordinary full-width path executes its band once.

A fused tap/output prototype was pixel-exact but did not provide a useful broad
speed gain. It was rejected; the vectorizable accumulation-row pass remains.

## Component timings

Windows 11, GCC 16.2.0, Qt 6.11.2, Release. Cells below are medians of three
resampling calls, in milliseconds, for generated 1920×1024 input enlarged to
10000×5333. ARGB32 varies alpha. Gamma and sharpening are off. Decoding, encoding,
source generation, output hashing and viewport painting are outside resize timing.

| Filter | RGB32 before | RGB32 candidate | ARGB32 before | ARGB32 candidate |
| --- | ---: | ---: | ---: | ---: |
| Hermite | 837.41 | 858.69 | 1013.60 | 853.18 |
| Triangle | 895.91 | 890.01 | 973.17 | 835.94 |
| Bell | 973.03 | 905.84 | 1060.19 | 933.23 |
| B-Spline | 1177.01 | 1115.24 | 1221.01 | 1033.47 |
| Mitchell | 1158.67 | 1092.26 | 1210.38 | 1121.53 |
| Lanczos | 1420.98 | 1362.10 | 1480.11 | 1340.84 |
| Nearest / pixel doubling | 535.50 | 91.64 | 663.99 | 91.94 |

The strongest gain here is Nearest: about 5.8× for RGB32 and 7.2× for ARGB32.
Filtered gains are much smaller, and some scenarios do not improve beyond
measurement noise. The RGB32 Hermite sample is about 2.5% slower; the quality
B-Spline downsample is about 8% slower in this run. This is not a claim that every
resize becomes faster. The earlier unchanged baseline run measured RGB32 Lanczos
at 834 ms, versus 1421 ms in the current control, demonstrating substantial host
drift. Compare the current control with its adjacent candidate, rather than the
previous night's numbers. There is no CPU/frequency isolation or matched
IrfanView comparison.

All 49 full output hashes match both unchanged baseline runs. The matrix covers
all seven methods, RGB32/ARGB32 upscaling and quality downscaling, plus gamma
upscaling with RGB32, ARGB32 and RGBA64. Raw samples and dimensions are in
`build/resize2-before-{1,2}.json` and `build/resize2-after-2.json`. Those component
measurements precede the final wide-stripe scheduling adjustment; their
full-width path and pixel arithmetic are unchanged. Twelve additional full
wide-output hashes match before/after that scheduling adjustment in
`build/resize2-wide.json` and `build/resize2-wide-final.json`.

## Proportional large outputs

These are real generated-image resizes, with file-backed output and a 512 MiB
operation workspace setting, not an accounting-only example or an enlarged
preview. Input is 1920×1024 ARGB32; Lanczos, gamma/sharpening off.

| Target | Native pixel bytes | Separate wide bands, seconds | Adjacent stripes, seconds |
| --- | ---: | ---: | ---: |
| 50000×26667 | 5,333,400,000 | 31.716 | 29.649 |
| 100000×53333 | 21,333,200,000 | 309.624 | 161.997 |

The 100,000-pixel run improves by about 1.9× in these sequential local runs.
Each cell is one run, not a statistically isolated disk benchmark; OS caching,
background load and thermal behavior can affect it. The resize timer includes
output allocation/reservation and mapped writes, but excludes final unmapping,
deleting the temporary result, and encoding a saved image. The complete final
100,000-pixel probe process takes 168.182 seconds. Nearest, whose code does not
change in the stripe-scheduling adjustment, completed the proportional
100000×53333 target in 60.686 seconds of resizing.

Each large probe checks 57 reference points, including column boundaries,
interior rows and the last pixel. They are not full byte comparisons of these
multi-billion-pixel outputs. Full comparisons use bounded fixtures up to 100,000
pixels wide, including 16-bit mapped output. Every probe releases its backing
file and returns mapped accounting to zero. No user images are used.

Windows process counters observed the final 100,000-pixel probe at 138,719,232
bytes of private usage and 12,525,531,136 bytes of peak working set, including
mapped pages. Its aggregate CPU time is 435.859 seconds. At 50,000 pixels the
observed private peak is 100,655,104 bytes and working set is 5,382,037,504 bytes.
These are sampled probe-process counters, not a strict process memory limit or
viewer benchmark. The OS still manages mapped pages; a 512 MiB image-work budget
does not keep total RSS below 512 MiB. Large output remains expensive. PNG can
save this checked geometry; JPEG remains limited to 65,500 pixels per axis.

Evidence: `build/resize2-measurements-summary.json`, `resize2-locality-summary.json`,
`resize2-large-{50000,100000}-5.json`, `resize2-large-100000-6.json` and
`resize2-locality-{50000,100000}.json`.

## Verification and reproduction

All 663 Qt cases in nine suites and all 46 Python tests pass in the development
build, with no failures or skips. The same Qt cases pass SDK-free in the portable
runtime, including installed modules, OCR/QR, five OCR language models and
profile relocation. Targeted native Windows 11 resize coupling, fitted-wheel
navigation, mapped viewport painting and original-pixel Undo pass at 100%, 125%,
150% and 200% scaling. These are targeted native checks, not every GUI test.

The full required-asset validation and portable/native results are recorded in
`build/review-resize2-final-validation-summary.json` and
`build/resize2-runtime-summary.json`. Regression coverage includes all filters
under parallel load, simultaneous operations, native pixel sizes and monochrome
rows, byte ties, transparent ringing, every 16-bit input value, column/row tile
boundaries, mapped lifetime and ordinary full-width termination.

```powershell
./build/ixview_benchmarks.exe build/resize-matrix.json --profile-resize-matrix -platform offscreen
./build/ixview_benchmarks.exe build/resize-wide.json --profile-resize-wide -platform offscreen
# Manual large probe: reserves/writes about 21 GB of temporary pixel storage.
# Requires at least twice that free disk space; removed when the probe ends.
./build/ixview_benchmarks.exe build/resize-100k.json --probe-resize-axis=100000 --resize-filter=5 -platform offscreen
```

The standard matrix makes three calls per scenario; wide mode uses Lanczos,
Triangle and Nearest at 50,000/100,000-pixel widths with bounded total pixels.
Large probes are opt-in and are not run by ordinary CI/core tests. Native Linux,
macOS and Windows 10 execution remain unverified locally. ARM keeps scalar
quantization; new x86 throughput measurements do not imply an ARM speedup.
No runtime dependency, startup worker, commit or push is added. Version remains
0.5.4, with changes under Unreleased for the user's GitHub Desktop workflow.

Further costs remain: output gamma transfer still uses the original per-pixel
function, optional sharpening needs full RAM work buffers, and encoding or
unmapping/deleting a multi-GB result can add substantial time. This pass does
not introduce deferred/non-destructive resizing or a strict mapped-page RSS cap.
