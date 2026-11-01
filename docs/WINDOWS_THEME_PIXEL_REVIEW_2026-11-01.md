# Windows title bars and native pixel edits · 1 November 2026

Version 0.5.0; local work on `main`, no push. README text/image markup and its
two image assets are preserved. No dependencies or downloadable components added.

## Native Windows title bars

Previously, appearance selected a Qt palette and color scheme without explicitly
synchronizing the native caption. Windows 10 can therefore retain a light title
bar with a dark client area. `platform_support.cpp::applyNativeWindowTheme`
sets DWM attribute 20, with attribute 19 as the older Windows 10 fallback. This
matches [Qt 6.8's compatibility approach](https://raw.githubusercontent.com/qt/qtbase/6.8/src/plugins/platforms/windows/qwindowswindow.cpp).
Microsoft describes the native caption mechanism in its
[Win32 theme guide](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-windows-themes).

`Appearance` handles existing Qt windows, later Qt dialogs and recreated native
handles. A widget-owned, coalesced callback applies the current choice after
Qt's own platform handler, guarded against destruction. It does not create
handles, resize, activate, hide/show or change window flags. Normal image loads,
filename/title updates and paints do not run the frame hook. Popups/frameless
widgets and non-Windows backends retain their normal behavior.
OS-owned shell dialogs retain their platform's appearance policy.

The native Windows 11 regression reads DWM state across light/dark transitions,
checks unchanged handles/geometry, a new dialog and frame recreation. It passes
at 100/125/150/200 percent scaling. Fullscreen transition/fit checks also pass.
Offscreen/non-Windows branches check safe no-op behavior without creating a
handle. **No Windows 10 machine was available**: the legacy fallback is included,
but actual Windows 10 caption painting still needs confirmation. Native flags
and geometry tests are not visual proof on every compositor/theme combination.

Evidence: `build/review-titlebar-final-validation-summary.json`,
`build/review-titlebar-native-*.txt`, `build/review-titlebar-fullscreen.txt` and
`build/theme-pixel-native-title-*.txt`. Code commit: `be7a63e`.

## Pixel editing and memory

Negative, channel display and channel swapping now process straight RGB32/ARGB32
with byte-exact bit operations in one output buffer. Other formats keep the
legacy RGBA64 route. Grayscale and arbitrary color adjustment are unchanged.
Alpha, profile, DPI, text, device-pixel ratio, offset and original pixels retain
their previous semantics, including source row padding and opaque RGB32 alpha.
Channel validation remains before indexing/shifting.

For a 24 MP native 32-bit input, the old source + RGBA64 + final output required
an estimated 384 MB of overlapping image buffers; the direct path needs 192 MB.
That is **16 → 8 bytes/pixel**, not a total-process/peak-RAM measurement. The
512 MiB operation-local image-buffer guard remains. Three 8193 × 4096 cases
failed before with the old budget and now succeed; 8193 × 8192 inputs remain
rejected before an additional output allocation. Decoder, history, other jobs,
metadata and allocator overhead have separate costs.

Ten format cases compare all supported channel arguments and swap combinations
with an independent retained legacy implementation, including premultiplied,
indexed, 16-bit and floating-point inputs. Additional cases cover external row
padding/properties, accepted large native inputs and the over-budget boundary.
Evidence: `build/pixel-edits-before-tests.txt` (three expected failures),
`build/review-pixel-edits-validation-summary.json` and quality-suite reports.
Code commit: `dcec181`.

## Measurement

Windows 11, Qt 6.11.2. Generated 6000 × 4000 images already in memory. Same
profiler in four fresh processes: before, after, before, after. Each process
alternates three legacy-reference/production calls per case; medians aggregate
six production calls per arm. Allocation, working/output conversion and editing
are timed; generation, decoding, IO and correctness checks are excluded. No
concurrent build/test run. Samples validate exact pixels and properties.

| Input | Operation | Before ms | After ms | Factor |
| --- | --- | ---: | ---: | ---: |
| RGB32 | Negative (all RGB) | 121.10 | 54.76 | 2.21× |
| RGB32 | Alpha channel | 115.31 | 62.02 | 1.86× |
| RGB32 | Swap red/blue | 212.21 | 85.64 | 2.48× |
| ARGB32 | Negative (all RGB) | 144.84 | 50.58 | 2.86× |
| ARGB32 | Alpha channel | 139.01 | 57.22 | 2.43× |
| ARGB32 | Swap red/blue | 232.74 | 82.05 | 2.84× |

The unchanged legacy control varies: RGB32 reference medians are about 5–9%
higher in the after arm, while ARGB32 controls are close. These small samples
establish component gains in this fixture, not cold-start, folder-navigation,
whole-batch or cross-platform speedups. The retained before executable and all
raw values/hashes are referenced by `build/pixel-edits-comparison.json`.

An earlier general 32/64-bit adapter was rejected: its grayscale prototype
took 336.62 ms versus 247.77 ms for the legacy path (three alternating samples).
Saving a buffer did not justify that timing regression. Evidence:
`build/pixel_candidate.cpp` and `build/pixel-candidate.json`. This prototype is
not production code.

```powershell
./build/ixview_benchmarks.exe build/pixel-edits.json --profile-native-pixel-edits -platform offscreen
```

## Verification and package

All **8 Qt suites / 532 cases** pass in the build and portable runtime without
SDK paths, with no skips. Counts: 200 functional, 143 workflow, 72 quality,
12 storage, 29 file-operation, 17 extension, 15 maintenance, 44 UI quality.
All **45 Python tests** pass. Counts include setup/cleanup and data rows.
Portable QR, five OCR languages, profile relocation and optional-module loading
pass; targeted native title/navigation cases pass at four scalings.

Evidence: `build/review-pixel-edits-validation-summary.json`,
`build/theme-pixel-runtime-summary.json`, `build/portable-*test-results.*` and
`build/theme-pixel-native-*.txt`. The documentation-only final validation uses
`build/review-theme-pixel-final-validation-summary.json`.

Build and deployed executable match, with valid Windows version information
`0.5.0.0`. Final executable SHA-256:

`da5636ad10984591687ae0996b908911712af9f291912a92f6e2886425dbb368`

Local Defender scans of the EXE and runtime report no threats. The build remains
unsigned; no vendor submission or public upload was performed. Canonical
outputs remain `dist/uxzView`, `dist/uxzView-win64.zip` and
`dist/uxzView-source.zip`; archive/source selection and checksums are verified
after documentation synchronization. Personal portable data/tools are retained.
Windows 10 visual validation and native Linux/macOS execution remain open.
