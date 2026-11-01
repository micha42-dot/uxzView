# Resource and package review — 1 November 2026

## Native grayscale

RGB32/ARGB32 grayscale now uses one native output buffer and integer arithmetic.
The rare exact-half case retains the legacy floating-point expression, preserving
its rounding. Every one of the 16,777,216 RGB colors matches the independent legacy
RGBA64 path. Additional tests cover alpha, padding, image properties, ten formats,
the newly accepted 8193 × 4096 native case and rejected over-budget buffers.
Other input formats retain the previous conversion path.

For a 24 MP 32-bit source, estimated overlapping image buffers fall from 384 MB
(source + 16-bit intermediate + output) to 192 MB (source + output). This is not a
whole-process RAM measurement. Existing caches and other edits have separate costs.

Four fresh profiler processes alternate before/after/before/after, each alternating
three legacy/current calls per format. Allocation and conversion are included;
decode, file IO, GUI and correctness comparisons are excluded from timing.

| Source | Before ms | After ms | Factor |
| --- | ---: | ---: | ---: |
| RGB32, 24 MP | 335.87 | 104.50 | 3.21× |
| ARGB32, 24 MP | 304.41 | 101.14 | 3.01× |

Medians aggregate six production samples per arm. Legacy control medians drift
from 320.84 to 338.54 ms (RGB32) and 289.09 to 391.80 ms (ARGB32), so these small
samples are component evidence, not stable whole-application or cross-platform
speedup claims. Raw samples: build/grayscale-*-before.json and *-after.json;
summary: build/grayscale-comparison.json. Reproduce with the same profiler:

```powershell
./build/ixview_benchmarks.exe build/grayscale.json --profile-grayscale -platform offscreen
```

## Windows deployment

Exiv2 0.28.9 is rebuilt from pinned source for local metadata input, without its
HTTP/Curl readers. XMP, PNG, BMFF/Brotli, video metadata and NLS remain enabled.
Gettext build tools are pinned SDK dependencies; they are not runtime payloads.
Source hashes, recipe, options, SDK lock and output hash are recorded alongside
the existing HEIF/AVIF provenance. Windows retains its native network-information
plugin and omits the redundant GIO backend. Qt's dynamic OpenSSL dependencies are
explicitly retained; the portable extension-manager HTTPS check passes.

The runtime changes from 189.68 to approximately
182.02 MB before this report is copied: about
7.66 MB saved, with 14 fewer DLLs. All 97 format capability records
match exactly. PDF, both recognition engines and five OCR languages remain.
The viewer budget returns to 160 MB plus 25 MB for tools. Unicode data, image
codecs, fonts and language resources are preserved. The 33 MB ICU data library
would need a separately validated Qt-build strategy for a substantial reduction.

## Verification

Build and portable runtime each pass all 8 Qt suites: 551 cases, no skips
(200 functional, 154 workflow, 78 quality, 12 storage, 29 file-operation,
17 extension, 15 maintenance, 46 UI quality). All 46 Python tests pass.
Portable OCR/QR, relocated profiles/tools, HTTPS availability and three lazy
modules pass without SDK paths. Native Windows regressions pass at four scales.
No new Windows 10, Linux or macOS run was available.

Evidence: build/review-resource-corrected-validation-summary.json,
build/resource-runtime-summary.json, build/resource-size-comparison.json and
referenced logs. Windows metadata remains 0.5.0.0; build and runtime
executables match, with explicit clean local Defender scans. Executable SHA-256:

`8fe14d596adf18bc717d744a4aa35b39904d4a2449afdd137efd623ed03a4f59`

This review precedes the separately requested dynamic-RAM/swap work. Its existing
operation limits are intentionally unchanged in this commit.
