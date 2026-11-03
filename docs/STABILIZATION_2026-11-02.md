# Technical stabilization — 2026-11-02

## Scope and baseline

This is the first bounded implementation block from the broader nine-phase task:
CI diagnosis, discard safety, a shared transaction-wait path and one measured
interaction improvement. It is **not completion of the full architecture,
aggregate-memory, decoder-security, format-expansion or deployment-layout plan**.
Version remains 0.5.4 with Unreleased changes. No commit or push was made.

Baseline source: `3f9ce53`. The user supplied failures from
[Desktop builds run 41458328431](https://github.com/micha42-dot/uxzView/actions/runs/41458328431).
The Windows job passed there. Linux workflow tests timed out; Apple Silicon
stopped during `imageOpenDialogIsPreparedBeforeShowing(select)`; both Mac large
image suites failed at `lifetimeAndHeader`, before any large image was allocated.
The Linux summary does not identify its final case. Its anonymous detailed log
was not accessible, so the same dialog cause on Linux remains a hypothesis to
confirm on the next run.

Before modification, the selected picker and mapping paths also passed locally
on Windows 11. This demonstrates why local success alone was insufficient.
Baseline commands/results are in `build/stabilization-baseline*.{json,txt}`.
Native Linux/macOS execution is unavailable locally; no WSL distribution was
available. The upstream Qt source comparison used the exact CI version, 6.8.3.

## Implemented fixes and their evidence

### File-picker test timeout

Qt 6.8.3 `QFileDialog::selectFile` deliberately does not replace a focused visible
filename editor. The old driver called it on a shown picker, waited an arbitrary
40 ms, then accepted. A host that focused the editor could leave an empty
selection and an open modal dialog indefinitely.

The driver now focuses and types into the editor as a user would, then queues
acceptance. It exercises a filename containing spaces and Japanese characters.
Its existing checks for selected pixels, retained edits after cancel, filter
labels, directory, initial size and zero visible resizing remain. A bounded
10-second driver reports unexpected messages or a selection failure and rejects
the test dialog; it does not turn that failure into success. Suite timeout stays
300 seconds. This changes test automation, not how users select files.

Reference: [Qt 6.8.3 QFileDialog source](https://github.com/qt/qtbase/blob/v6.8.3/src/widgets/dialogs/qfiledialog.cpp).

### macOS mapped-buffer volume check

The failure comes from the application's space/read-only guard, not from a
failed multi-GB allocation. Qt 6.8.3 resolves a path to its Core Foundation volume
URL before querying it. On APFS/firmlink layouts that can identify the sealed
system volume rather than the writable filesystem used by the backing file.
The supplied old log does not include the individual volume values, so this
specific APFS mechanism still needs confirmation on native CI.

macOS now uses `fstatfs` on the already-open backing file descriptor. The code
rejects failed/overflowing queries, read-only filesystems and insufficient space.
It retains the 64 MiB free-space margin, aggregate disk budget, owner-only
permissions, real physical preallocation, mapping accounting and cleanup.
Windows/Linux retain Qt's query, now against the opened file's directory.
A failed query logs validity, read-only state, required/available bytes and path.
The test logs Qt's resolved temporary volume; caught C++ exceptions return a
failure code with their message instead of an unexplained abort.

Reference: [Qt 6.8.3 macOS storage source](https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/io/qstorageinfo_mac.cpp).
A real lack of space will still fail. This change does not fake capacity or
bypass reservation. The macOS branch has **not** been executed locally.

### CI diagnostic checkpoints

Workflow and large-image tests log entry to each case (`-v1`). Failure reporting
starts with the last recorded checkpoint for every suite, before full build
output. Full reports remain intact, including partial/corrupt UTF-8 logs. Unix
CI prints capacity for the checkout and temporary directory. Existing suite
limits and optional-feature requirements were not relaxed.

### One discard decision

Absent `navigation/askToSave` now means true, both in Settings and at runtime.
Explicit false/true values remain untouched. Document replacement and closing
share one decision instead of separate implicit Ask/UsePreference paths.
Only an explicit Discard or a successful Save authorizes proceeding. Cancel,
dialog rejection/unknown answers and failed saves do not authorize data loss.

New-profile coverage checks open, folder navigation, reload, page change,
close, Escape, Exit, Clear and slideshow advance. It retains pixels, selection,
page and both history directions after cancellation. Slideshow confirmation
occurs before its first departure from the edited image; cancel stops it.
A Save-during-close regression changes the target externally during overwrite
confirmation and verifies that the newer file, unsaved pixels and Undo/Redo all
survive the rejected save. Existing discard-mode tests configure false explicitly;
the feature was retained, not skipped. Opening Settings and canceling does not
write a default preference.

### Shared progress/future waiting

Uncancellable save/trash/move/copy operations join through `worker_wait.h` rather
than an additional QDialog::exec loop. A future finishing during native show must
not close a dialog before its modal loop has begun. The new helper joins the
future's own event loop and displays progress after 200 ms only if work remains.
Existing activity gates, GUI timers, input suppression, revisions and atomic
commit paths remain. The helper has tests for pre-completed work, fast work,
completion during Show and slower work. Cancellable extraction is a separate
path and has not been migrated in this block.

## Measured interaction improvement

Environment: Windows 11 Pro 10.0.26300, Intel Core i5-10210U (4 cores/8 threads),
about 16 GiB installed RAM, GCC 16.2 Release, Qt 6.11.2. The profiler uses Qt's
Windows widget style, a warmed writer and 20 alternating pairs in one process.
Each pair atomically saves identical generated 128 x 96 ARGB32 PNG data. The
saved pixels are decoded and checked outside the timer. No user files are used.

| Save plus GUI wait | Previous dialog wait | New future wait |
| --- | ---: | ---: |
| Samples | 20 | 20 |
| Median | 19.529 ms | 6.314 ms |
| Sample p95 | 34.616 ms | 9.528 ms |
| Progress windows shown | 20 | 0 |
| Watchdog failures | 0 | 0 |

These are local warm component measurements with 20 samples per path, not a
cold-start, whole-app save, decoding, navigation or rendering speedup claim.
Confirmation dialogs and final pixel validation are excluded. Legacy watchdog
failures would be reported separately, never counted as speedup samples.
No CPU/frequency isolation was applied; the small-sample p95 is descriptive.
The verified benefit is avoiding unnecessary progress-window creation for fast
transactions while retaining feedback for slow ones. No startup/image-open/
folder-navigation speed improvement is claimed in this block.

```powershell
./build/ixview_benchmarks.exe build/save-progress.json --profile-save-progress -platform windows
```

Raw data: `build/stabilization-save-progress.json` and
`build/stabilization-save-progress-summary.json`.

## Changes and reproducible validation

- `tests/workflow_ixview.cpp`: focused picker automation, deadlines, new-profile
  consent and failed-save history coverage.
- `src/core/image_storage.cpp`, `tests/large_images_ixview.cpp`: file-descriptor
  volume query, diagnostics and controlled test-exception reporting.
- `CMakeLists.txt`, `.github/workflows/desktop.yml`,
  `scripts/report_test_logs.py`, `tests/test_ci_diagnostics.py`: entry checkpoints,
  capacity context and failure synopsis.
- `src/ui/main_window{,_files}.cpp`, `main_window.h`, `settings_dialog.cpp`:
  one discard policy, safe default and shared transaction waiting.
- `src/ui/worker_wait.h`, `tests/ui_quality_ixview.cpp`: delayed progress and
  completion-during-show coverage.
- `tests/test_ixview.cpp`, `scripts/validate.py`: explicit legacy fixtures,
  current default assertions and renamed configured-discard test.
- `tests/bench_ixview.cpp`: real-save comparison; existing resize profilers remain.
- Settings, architecture, changelog, README and this report document the change.

```powershell
cmake --build build --parallel 4
python -B scripts/validate.py --suite core --require-assets
```

All **676 Qt cases in nine development suites and 47 Python tests pass**, without
failures or skips. RAW and recognition assets are required. Regression tests also
retain existing stale-load, deferred-open, revision, save, cancel, history,
format, pixel-precision and mapped-lifetime checks. Results:
`build/review-stabilization-final-validation-summary.json`.
The Windows baseline, targeted policy checks and native save measurements are
also retained under `build/stabilization-*`. All tests pass locally; the next
native Linux/Intel-Mac/ARM-Mac CI runs are still required. ASan/UBSan and native
fuzzing were not performed in this block.

The deployed Windows runtime also passes all nine suites (676 cases, no failures
or skips) without the SDK in PATH. Bundled QR/OCR engines, all five OCR models,
relocated tools and portable-profile startup pass. Installed optional modules
load successfully. Native Windows dialog, discard-policy and progress checks
pass at 100%, 125%, 150% and 200% scaling. The deployed executable matches the
validated development executable byte for byte. See
`build/stabilization-runtime-summary.json` and `build/stabilization-portable.log`.

## Deployment assessment: current structure retained

The root contains **79 DLLs**: seven Qt runtime, three compiler runtime, three
optional modules and 66 third-party runtime DLLs. Reading the actual PE import
graph identifies **33 packaged startup dependencies** and 46 other root DLLs.
The full snapshot is below and in `build/stabilization-dll-inventory.json`.

```text
uxzView/
  uxzView.exe
  qt.conf
  *.dll                 # 79; includes ixrawpreview/ixpdf/ixextensions
  platforms/ imageformats/ styles/ printsupport/ iconengines/
  tls/ networkinformation/ libheif/
  tools/ translations/ licenses/ docs/
```

No DLL was relocated and the clean-root acceptance criterion is **not yet met**.
No launcher, loader-search change, duplicate runtime DLL or new dependency was
introduced. Existing private portable data/tools remain outside managed cleanup.

| Alternative | Assessment for the next deployment block |
| --- | --- |
| A: executable with startup dependencies; move optional components | Least invasive starting point; moving only three modules is insufficient cleanup. |
| B: internal real executable with a root launcher | Can clean the root most, but requires independent Unicode arguments, exit codes, working directory, shell identity, read-only and portable-profile tests. Not justified as an unverified shortcut. |
| C: trusted search directories for dynamic dependencies | Candidate alongside A for the 46 non-startup root DLLs; must initialize before Qt/plugin loading and prove dependency closure in SDK-free tests. It cannot repair missing early executable imports. |

The 33 startup dependencies are resolved before application main. Moving them
without a bootstrap/launcher or another proven loader arrangement breaks startup;
qt.conf does not configure that loader. Runtime tests alone do not prove a
hypothetical relocation safe. Windows registration, process identity, plugin
paths, optional-module locations, license/source manifests and tools remain as
before. Read-only-directory and shell-association behavior were not newly
reproduced in a clean VM in this block.

## Remaining work, ranked

| Priority | Work | Expected benefit | Cost / regression risk |
| --- | --- | --- | --- |
| 1 | Re-run native CI, inspect actual Mac volume values and Linux checkpoint | Close the reported platform failures with native evidence | Small / low |
| 2 | Separate loading/navigation intent and save completion from MainWindow through explicit ownership/state contracts; audit remaining cancellable waits | More predictable reentrancy, cancellation and maintenance | Medium-large / medium-high |
| 3 | Coordinate concurrent decoder/transform/batch reservations with caches and retained history | Reduce cumulative memory pressure | Large / high; third-party allocations and mapped pages are not fully controllable |
| 4 | Implement and validate A+C deployment cleanup using the dependency inventory | Substantially fewer root DLLs without unnecessary launcher complexity | Medium-large / high loader/security risk |
| 5 | Add sanitizer/fuzz jobs and targeted malformed-input coverage | Decoder and arithmetic reliability | Medium / toolchain/platform dependent; native decoder crashes remain in-process |
| 6 | Verify ImageGlass comparison, JPEG XR first, then WMF/EMF/FITS and candidate aliases with real fixtures | Broader useful format support | Medium-large / parser, licensing, platform and resource risks |
| 7 | Expand reproducible everyday startup/open/navigation/rendering benchmarks and optimize measured bottlenecks | User-perceived speed beyond short transactions | Medium / medium |

No new image format or alias was registered. No ImageGlass parity claim is made.
The requested extension-by-extension compatibility/license review remains open.
No video subsystem was added. The full MainWindow extraction and coordinated RAM
work also remain open. At the end of this initial block, BatchPipeline bounded
worker count to four useful jobs rather than combined live memory; caches,
active documents, history, codecs and mapped pages could accumulate. Per-operation budgets
are not a global RSS limit. This pass introduces no decoder process isolation or
claim of complete protection from third-party crashes.

Follow-up: [batch memory admission](BATCH_MEMORY_2026-11-02.md) now coordinates
estimated decoder/transform/export work across pipeline runs. The global RSS,
cache/history and third-party-allocation limitations above still apply.
The later [cache/navigation step](EVERYDAY_PERFORMANCE_2026-11-02.md) also accounts
for retained image-cache cost at batch admission; full process coordination remains open.

## Root DLL snapshot

| File | Class | Startup import closure |
| --- | --- | --- |
| `ixextensions.dll` | optional module | no |
| `ixpdf.dll` | optional module | no |
| `ixrawpreview.dll` | optional module | no |
| `libaom.dll` | third-party runtime | no |
| `libavif-16.dll` | third-party runtime | no |
| `libb2-1.dll` | third-party runtime | yes |
| `libbrotlicommon.dll` | third-party runtime | yes |
| `libbrotlidec.dll` | third-party runtime | yes |
| `libbrotlienc.dll` | third-party runtime | no |
| `libbz2-1.dll` | third-party runtime | yes |
| `libcrypto-3-x64.dll` | third-party runtime | no |
| `libcryptopp.dll` | third-party runtime | no |
| `libdav1d-7.dll` | third-party runtime | no |
| `libde265-0.dll` | third-party runtime | no |
| `libdeflate.dll` | third-party runtime | no |
| `libdouble-conversion.dll` | third-party runtime | yes |
| `libexiv2-28.dll` | third-party runtime | yes |
| `libexpat-1.dll` | third-party runtime | yes |
| `libfreetype-6.dll` | third-party runtime | yes |
| `libgcc_s_seh-1.dll` | compiler runtime | yes |
| `libglib-2.0-0.dll` | third-party runtime | yes |
| `libgomp-1.dll` | third-party runtime | no |
| `libgraphite2.dll` | third-party runtime | yes |
| `libharfbuzz-0.dll` | third-party runtime | yes |
| `libheif.dll` | third-party runtime | no |
| `libhwy.dll` | third-party runtime | no |
| `libiconv-2.dll` | third-party runtime | yes |
| `libicudt78.dll` | third-party runtime | yes |
| `libicuin78.dll` | third-party runtime | yes |
| `libicuuc78.dll` | third-party runtime | yes |
| `libIex-3_5.dll` | third-party runtime | no |
| `libIlmThread-3_5.dll` | third-party runtime | no |
| `libImath-3_2.dll` | third-party runtime | no |
| `libinih-0.dll` | third-party runtime | yes |
| `libINIReader-0.dll` | third-party runtime | yes |
| `libintl-8.dll` | third-party runtime | yes |
| `libjasper.dll` | third-party runtime | no |
| `libjbig-0.dll` | third-party runtime | no |
| `libjpeg-8.dll` | third-party runtime | no |
| `libjxl.dll` | third-party runtime | no |
| `libjxl_cms.dll` | third-party runtime | no |
| `libjxl_threads.dll` | third-party runtime | no |
| `libKF6Archive.dll` | third-party runtime | no |
| `libkvazaar-7.dll` | third-party runtime | no |
| `liblcms2-2.dll` | third-party runtime | no |
| `libLerc.dll` | third-party runtime | no |
| `liblzma-5.dll` | third-party runtime | no |
| `libmd4c.dll` | third-party runtime | yes |
| `libmng-2.dll` | third-party runtime | no |
| `libOpenEXR-3_5.dll` | third-party runtime | no |
| `libOpenEXRCore-3_5.dll` | third-party runtime | no |
| `libopenh264-7.dll` | third-party runtime | no |
| `libopenjp2-7.dll` | third-party runtime | no |
| `libopenjph-0.32.dll` | third-party runtime | no |
| `libpcre2-16-0.dll` | third-party runtime | yes |
| `libpcre2-8-0.dll` | third-party runtime | yes |
| `libpng16-16.dll` | third-party runtime | yes |
| `libraw-25.dll` | third-party runtime | no |
| `libraw_r-25.dll` | third-party runtime | no |
| `libsharpyuv-0.dll` | third-party runtime | no |
| `libssl-3-x64.dll` | third-party runtime | no |
| `libstdc++-6.dll` | compiler runtime | yes |
| `libtiff-6.dll` | third-party runtime | no |
| `libturbojpeg.dll` | third-party runtime | yes |
| `libwebp-7.dll` | third-party runtime | no |
| `libwebpdemux-2.dll` | third-party runtime | no |
| `libwebpmux-3.dll` | third-party runtime | no |
| `libwinpthread-1.dll` | compiler runtime | yes |
| `libx264-165.dll` | third-party runtime | no |
| `libyuv.dll` | third-party runtime | no |
| `libzstd.dll` | third-party runtime | yes |
| `Qt6Core.dll` | Qt runtime | yes |
| `Qt6Gui.dll` | Qt runtime | yes |
| `Qt6Network.dll` | Qt runtime | no |
| `Qt6Pdf.dll` | Qt runtime | no |
| `Qt6PrintSupport.dll` | Qt runtime | yes |
| `Qt6Svg.dll` | Qt runtime | yes |
| `Qt6Widgets.dll` | Qt runtime | yes |
| `zlib1.dll` | third-party runtime | yes |
