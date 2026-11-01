# Large resize performance review

The reported case is reproducible: an already loaded 1920 × 1024 image enlarged
to a proportional 10000 × 5333 target (53.33 MP), using the dialog's default
Lanczos filter. The initial RGB32 measurement takes about 4–6 seconds in the
resampling function, while fitted viewport painting takes about 5–9 ms. File
loading, saving and GUI state changes are outside these component timings.

## Changes

`core/resampling.cpp` now owns kernels, row pipelines and resize budget logic;
`processing.h` retains the API. Source normalization/premultiplication is reused
within each source row; cached horizontal results keep the original RGBA64
rounding boundary, then store normalized doubles for vertical reuse. The byte
output path avoids repeated library rounding at ordinary values and falls back
at floating-point ties and near-zero-alpha overflow cases. This preserves even
hidden RGB values under zero alpha.

Large outputs use at most four disjoint row bands. A shared atomic allowance
caps additional threads across concurrent resize calls. The caller participates;
std::thread and a scope guard join all children before exposing output or freeing
mapped pixels. Workers use private caches/sums and raw non-overlapping destination
rows. Cancellation/exception handling never publishes a partial result. The same
aggregate row-cache cap is divided between workers, and the GUI/worker estimate
includes their local buffers. Compact RGBA64 rows remain the fallback for very
wide rows or large kernels that would thrash normalized caching.

## Measurements

Windows 11, GCC 16.2.0, Qt 6.11.2, Release build. Each cell is the median of three
calls in one process. Fixtures are generated in memory; ARGB32 has varying alpha;
source ICC is sRGB; gamma correction and sharpening are off. Source generation and
hashing are outside resize timing; display preparation and painting have their
own timing fields. The unchanged
baseline executable was retained before edits (source base ddf4935).

| Format / filter | Original run 1, ms | Original run 2, ms | Final candidate, ms |
| --- | ---: | ---: | ---: |
| RGB32 / Lanczos | 4559.60 | 11183.18 | 1234.35 |
| RGB32 / Triangle | 4365.53 | 4764.10 | 431.06 |
| RGB32 / Nearest | 250.83 | 320.51 | 239.96 |
| ARGB32 / Lanczos | 4490.13 | 9271.45 | 1432.55 |
| ARGB32 / Triangle | 2637.96 | 5894.43 | 639.16 |
| ARGB32 / Nearest | 253.46 | 630.23 | 315.84 |

Host timing drift is substantial, including in the unchanged Nearest control.
These are not CPU-isolated paired measurements, peak-RSS measurements or an
IrfanView comparison under matched settings. Intermediate prototypes are not
used as the final candidate. Raw samples and exact parameters are in
`build/resize-before-1.json`, `build/resize-before-2.json` and
`build/resize-after-release.json`. Six complete output SHA-256 hashes match
the original executable for all filters/formats in this benchmark. The complete
53 MP transparency comparison also matches the independent legacy arithmetic;
`build/resize-diff-release.json` records zero differing pixels.

## Verification and scope

**605 Qt cases in nine suites and 46 Python tests pass**, with no skips. The same
605 Qt cases pass in the portable runtime without SDK paths. Installed modules,
OCR/QR, five language models, profile relocation and targeted native Windows
checks at 100/125/150/200 percent pass.

Added coverage compares parallel RGB32/ARGB32/RGBA64 results against independent
legacy output with gamma on/off, checks simultaneous resizes, evaluates the
reported 53 MP case at transparent-ringing points, and verifies a parallel mapped
result and final-reference cleanup. Existing small-format/filter/DPI/cancellation
and memory-boundary tests remain. `--profile-resize` reproduces the component
benchmark; `--check-resize` checks every output pixel against the legacy path and
returns nonzero on a mismatch. Both accept the JSON output filename first.

Current source and Windows packages retain version 0.5.4; these changes are under
Unreleased and remain uncommitted for the user's GitHub Desktop workflow.
No new runtime libraries or startup threads are introduced. Native Linux/macOS
execution and native Windows 10 remain unverified locally; portable C++20/Qt APIs
are used, including std::thread rather than newer libc++ stop-token facilities.
File loading, ICC conversion of non-sRGB output, optional sharpening and encoders
can add time beyond the resampling measurement. Gigapixel/format/storage limits
remain documented in [LARGE_IMAGES.md](LARGE_IMAGES.md).
