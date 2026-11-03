# Native 8-bit standard sharpening — 2026-11-03

Follow-up: [first-session startup and clipboard contention](STARTUP_CLIPBOARD_2026-11-03.md)
records a separate GUI-wait correction and the newer complete validation totals.

## Implementation and scope

The standard cross-shaped kernel `5*center - up - left - right - down` already
had an exact integer implementation, but ordinary RGB32/ARGB32 images still
expanded to RGBA64 and converted back afterward. Two large 16-bit working
buffers remained in that path.

RGB32 and straight-alpha ARGB32 now run directly in their native representation.
Expanding a byte by 257, applying this integer kernel, clamping to 0..65535 and
converting back is exactly `clamp(5*byte - neighborSum, 0, 255)`. Alpha remains
the original center alpha; RGB32 always receives opaque output alpha, including
when unused source alpha bytes contain arbitrary values.

On compiler targets with baseline SSE2, four interior pixels are processed
together. Signed 16-bit intermediate lanes stay within -1020..1275 and packing
saturates to the same byte values. Original alpha is restored after packing.
Unaligned loads/stores are supported. Row borders and the remaining columns use
the scalar path; vector loads never cross the image boundary. Other targets use
the same portable scalar calculation. There is no AVX requirement, runtime CPU
probe, new dependency, thread pool or image-sized scratch buffer.

Only the exact standard kernel, divisor 1 and offset 0 use this specialization.
Other kernels, premultiplied/indexed/grayscale input, 16-bit and floating-point
formats keep their existing processing paths. Output format, pixels, ICC, text,
DPI and the legacy device-pixel-ratio/offset behavior are unchanged. Source
pixels, custom row padding and image identity are not modified.

This benefits normal sharpening, enabled display sharpening, batch sharpening
and resizing followed by sharpening when the result is RGB32/ARGB32. It does
not accelerate decoding or a resize without sharpening. The existing conservative
working-set/admission checks remain in place: lower actual allocation is not
advertised as a larger supported-image limit. Global RAM coordination and native
Windows 10/Linux/macOS confirmation remain separate work.

## Pixel and portability checks

Two new cases exercise every center byte (0..255) against every possible sum
of four neighbor bytes (0..1020), in RGB32 and ARGB32, against the retained
general double/RGBA64 reference. Three differently permuted color channels and
varied alpha cover clamp thresholds and transparent hidden RGB values. Two
additional cases cover padded/unaligned rows, non-opaque unused RGB32 alpha,
ICC/text/DPI, device ratio/offset and unchanged external source bytes.

Existing reference checks also cover 1x1, single-row/column images, irregular
widths, the full 16-bit range, premultiplied/HDR/grayscale inputs and nonstandard
kernels. The 37-case focused run passes with SSE2 enabled:
`build/native-sharpen-sse-quality.txt`.

The scalar branch is compiled separately from the current source using a local
copy with only the private SIMD-selection definition disabled. Its preprocessing
output confirms `IX_SHARPEN_SSE2` is absent. Only that processing object replaces
the corresponding member in an isolated copy of the core archive. The same 37
cases pass: `build/native-sharpen-scalar-quality.txt`. This is branch validation
on Windows x64, not a native ARM/Unix build or a production configuration flag.
The validation copies are ignored build artifacts and are not shipped.

## Measurement method

```powershell
./build/ixview_benchmarks.exe build/sharpen.json --profile-native-sharpen 4 6000 4000 -platform offscreen
```

Formats 4/5 select RGB32/ARGB32; format 26 is the unchanged RGBA64 control. The
profiler generates native source pixels before timing and checks exact output
against the retained reference after timing and memory sampling stop. It times
allocation, filtering and format conversion, with Windows process CPU time and
private committed-memory growth sampled every 2 ms. Unsupported counters are
null. Source generation, verification, file decoding, resizing and GUI drawing
are outside the timer. CPU counters include sampler/runtime work and have coarser
resolution than the elapsed timer. Memory growth is not process RSS.

Three alternating before/after fresh-process pairs run each case. Both use the
same benchmark object, with frozen pre-change libraries:

- core: `703547a1a9944eca01984d7ff766c568fa93fec85b88f41daf0f44512ddd61a8`
- UI: `fd4d00a207d926013389000b8938923dd2d074e940fd8b8b3a68f4b3db91cc4e`

The baseline is the previous integer RGBA64 implementation, not the older
general scalar reference. Native x64 SSE2 measurements are recorded in
`build/native-sharpen-comparison.json` and `build/native-sharpen-summary.json`.
The earlier scalar-only native-buffer experiment is kept separately in
`build/scalar-native-sharpen-*.json`; its timings are not pooled with the final
SSE2 comparison.

## Local Windows measurements

Windows 11, Intel Core i5-10210U, approximately 16 GiB RAM, Qt 6.11.2 and GCC
16.2 Release. Three fresh-process samples per arm/case, alternating pair order.
CPU frequency/background activity were not locked. Medians:

| Case | Previous integer RGBA64 path | Native SSE2 path | Additional private commit, before / after |
| --- | ---: | ---: | ---: |
| RGB32, 3000 x 2000 | 162.18 ms | 21.32 ms | 96.47 / 24.15 MB |
| ARGB32, 3000 x 2000 | 172.20 ms | 22.87 ms | 96.24 / 24.13 MB |
| RGB32, 6000 x 4000 | 633.08 ms | 83.14 ms | 385.04 / 96.25 MB |
| ARGB32, 6000 x 4000 | 695.59 ms | 88.65 ms | 384.85 / 96.24 MB |
| RGBA64, 3000 x 2000, unchanged control | 122.52 ms | 117.73 ms | 48.15 / 48.19 MB |

The four byte cases are about 7.5–7.8 times faster, with about 75% less sampled
additional private commit. Source-buffer memory already exists at the sampling
baseline; these numbers are not total process memory. The 16-bit control keeps
the previous algorithm and has unchanged CPU median (125 ms) and memory charge;
the small elapsed-time difference is not claimed as an RGBA64 improvement.
Every before/after output hash matches and each output matches the independent
general reference, with its metadata/source checks outside timing.

The earlier portable scalar native-buffer version already reduced allocation
similarly, but its RGB32 24 MP median was 473.60 ms versus 628.13 ms baseline;
ARGB32 was 491.71 versus 689.87 ms. This measured remaining kernel cost motivated
the SSE2 addition. Scalar and final SSE2 timings are separate data sets.

These are filter-operation measurements, not a universal viewer/startup speedup.
Display sharpening must be enabled for that display path to benefit; normal
opening without sharpening keeps its existing work. Native ARM/macOS/Linux
performance has not been measured.

## Complete regression

The Release build passes all nine required-asset Qt suites: 713 cases, no
failures or skips, including 152 quality cases. All 47 Python tests pass.
Evidence: `build/review-native-sharpen-final-validation-summary.json`,
`build/ctest.log` and the per-suite result files. RAW and recognition fixtures
are required in this run. The measured executable is the validated build;
`build/review-native-sharpen-final-binary.json` records its SHA-256.

The deployed portable runtime passes the same 713 Qt cases without SDK paths,
failures or skips. Real QR/OCR engines, all five OCR models, relocated portable
profiles and installed native modules also pass. Evidence:
`build/native-sharpen-runtime-summary.json`, `build/native-sharpen-portable.log`
and `build/portable-*-results.txt`. The deployed executable matches the validated
build SHA-256; temporary test executables and QtTest are removed afterward.
The distribution contains 444 files, about 182.4 MB before compression; this
optimization reduces image working memory, not the Qt/tool package footprint.
