# Built-in jpegli, save preferences and settings — 2026-11-09

## Integration

Base: `51a7b36`, version 0.5.4 with Unreleased changes. Google jpegli is required
native code, statically linked through `ixcore`; it is not an extension or an
external executable. No Python runtime or additional end-user installation is
introduced. JPEG XL keeps its existing independent codec.

`scripts/native-sources-lock.json` pins jpegli revision
`031a0077f5799a6041004267fc12b956c1f52a20`, Highway revision
`271a9a0ed9de1232d9117f1572c3fe28f8542ec1` and the matching libjpeg-turbo API-header
revision `8ecba3647edb6dd940463fedf38ca33a8e2a73d1`, with archive SHA-256 hashes.
CMake uses the upstream library source list, omits the replacement `jpeg_*`
wrapper and isolates the generated libjpeg62 headers to `jpegli_encoder.cpp`.
Existing JPEG decoders and coefficient transforms retain their original headers
and libraries. CMake builds the same source path on Windows/Linux/macOS; Windows
bootstrap prefetches the source archives and CI cache keys include the new lock.

All regular JPEG aliases (`jpg`, `jpeg`, `jpe`, `jfif`) reach
`CodecRegistry::save` → `writeJpegli`, including normal Save, Save As, selected
regions, classic batch and processing chains. Existing quality integers are
passed to jpegli. There are no additional technical controls in the UI.

The writer uses sequential, non-optimized Huffman coding so jpegli can retain
MCU rows instead of full-image coefficients. This favors bounded working memory
and baseline compatibility; it does not claim the smallest possible jpegli file
or identical compression to libjpeg-turbo. A conservative width-based allowance
is shared with batch memory admission. Input conversion is row-based, including
transparent pixels on white; no full-image RGB flattening buffer is needed.
JPEG remains eight-bit and limited to 65,500 pixels per axis. Existing loss
confirmations for alpha, depth, metadata and page count remain in force.

RGB, native grayscale and CMYK are supported. CMYK uses the inverted channel
convention and Adobe marker expected by common readers, retaining CMYK ICC
profiles. Gray-profile data retain their matching profile. RGB ICC profiles,
UTF-8 image comments and ordinary JFIF DPI are written explicitly. JFIF does not
define CMYK: as before, CMYK resolution relies on EXIF metadata. Broader real
CMYK-profile/print-workflow checks remain useful beyond the synthetic CMYK test.

The central atomic QSaveFile, source/destination revision checks, metadata
attachment, overwrite questions and before-commit cancellation remain in charge.
IO and native encoder errors return before publication. The core also rejects a
requested encoder that disagrees with the filename extension.

## Save behavior

Settings → **Save** replaces Storage locations. Existing portable mode and data
folder actions remain. `save/preferOriginal` defaults to true when absent;
`save/format` defaults to PNG. Available format choices come from actual writable
codec capabilities. Writer discovery waits until the Save page is opened or its
mode is explicitly changed, keeping other settings pages independent of it.

| Situation | Result |
| --- | --- |
| Writable source, original preferred | Source type is preselected; normal edited Save can retain its path. |
| Fixed format differs from source | Save As suggests a new suffix/path; the source is retained. |
| Read-only source or missing preferred encoder | Save As selects a writable fallback, normally PNG. |
| Another type or supported suffix chosen in Save As | Only this export changes; the global preference is retained. |
| Missing/unsupported suffix | The selected type's extension is appended. |
| Existing destination | Existing app-side confirmation and revision guard apply. |
| Unchanged existing JPEG, normal Save | No recompression or format conversion. Save As remains explicit export. |
| Selection export | Only selected pixels are exported; active document and edit state remain. |

Unknown stored fixed formats survive a settings visit/cancel and a never-opened
Save page. Selecting an unavailable fixed format on that page requires choosing
an available one before accepting. New preference values participate in the
existing portable-profile copy transaction and its Cancel boundary.

The settings window starts at 760×460 logical pixels, with a flat rounded
category sidebar, tighter row/column spacing and unchanged minimum 32-pixel
control targets. Dropdown arrow glyphs remain 8×5 pixels. Switching categories
keeps window geometry stable; narrow displays retain per-page vertical scrolling.
No explanatory paragraphs or decorative group boxes were added.

## Licenses and release integrity

`docs/THIRD_PARTY.md` lists the new repositories, recognition engines/models and
direct build/CI tools. Generated `SOURCES.md` adds license and upstream columns
for every shipped DLL package, plus the pinned native components and tools.
The original jpegli license/authors/patents, Highway license and libjpeg header
license/readme texts come from verified archives. Windows deployment places them
under `licenses/native` and records notice/application hashes in
`native-dependencies.json`. Packaging rejects missing/changed notices or a stale
native source/application inventory. macOS installs native notices inside its
bundle; Linux uses `share/IxView/licenses/native`.

## Verification

Local component measurements use a deterministic 6000×4000 RGB pattern, with an
additional constant-alpha variant, quality 95 and QBuffer output. Each row is the
median of three fresh processes per encoder, alternating Qt/libjpeg-turbo and
jpegli. Source generation and QApplication setup are excluded; first encoder
initialization and compressed output allocation are included. Windows 11,
GCC 16.2, Qt 6.11.2. MB are decimal; private commit is the process peak delta,
not RSS or a global memory limit.

| Input | Qt time | jpegli time | Qt / jpegli output | Qt / jpegli extra peak private commit |
| --- | ---: | ---: | ---: | ---: |
| 24 MP opaque | 286.39 ms | 277.89 ms | 19.99 / 12.95 MB | 56.00 / 31.91 MB |
| 24 MP alpha | 315.16 ms | 314.20 ms | 15.34 / 9.93 MB | 127.23 / 31.90 MB |

The reference includes the old full-frame white composition for alpha. These
samples support a substantial memory improvement and comparable local encode
time. They do not establish equal visual quality at the same integer setting,
a universal compression ratio or faster decoding/startup. Raw measurements:
`build/jpegli-benchmark-results.json` and `build/jpegli-benchmark-summary.json`.

Dedicated saving tests cover direct-vs-central jpegli bytes, distinction from Qt
JPEG output, quality variation, ICC/text/DPI, white alpha composition at 8/16/float
precision, eight non-alpha layouts including CMYK, native and IO error recovery,
bounded working memory, suffix mismatch rejection, available formats, preference
roundtrip/Cancel, original/read-only/missing-format planning, actual Save/Save As/
selection routes, parallel JPEG calls, batch output and portable import.
Existing suites retain JPEG decoder/lossless, orientation, metadata, file safety,
other image formats and UI behavior coverage. The independent Qt alpha-composition
reference is compressed with jpegli for comparison; differing lossy encoders are
not required to produce identical decoded pixels.

The development and SDK-free portable runs each cover 898 Qt cases across ten
suites, including 28 saving cases, with no failures or skips. The Python suite
covers 50 cases. Evidence is recorded in `build/jpegli-verification.json` and
PROJECT_STATUS.md. The settings checks pass 15 cases per scale
at 1, 1.5 and 2 on native Windows 11. UI save-route automation uses Qt's widget
chooser; manual checks of native OS file choosers remain useful.
Native Windows 10/Linux/macOS execution and
their installed-runtime behavior remain unverified locally. CMake/platform wiring
does not substitute for those runs. No commit, push, publication or routine virus
scan is performed by this task.

## Changed files

| Files | Responsibility |
| --- | --- |
| `cmake/Jpegli.cmake`, `CMakeLists.txt` | Required static codec, private ABI headers, notices and saving test target |
| `scripts/native-sources-lock.json`, `fetch_native_sources.py`, `bootstrap_sdk.py` | Reproducible sources and cache population |
| `src/core/jpegli_encoder.{h,cpp}`, `save_formats.{h,cpp}` | Encoder, canonical formats and pure destination planning |
| `src/core/codec_registry.cpp`, `batch_memory.cpp` | Central dispatch, suffix guard and batch admission |
| `src/ui/main_window_files.cpp`, `settings_dialog.{h,cpp}` | Common saving behavior and compact/lazy settings |
| `resources/languages.json` | German/English/Korean/Japanese/French text |
| `scripts/native_dependencies.py`, `deploy.py`, `dependency_sources.py`, `package.py` | Native notices, full source inventory and release checks |
| `scripts/verify_portable.py` | Run the saving suite against installed DLLs without SDK paths |
| `tests/saving_ixview.cpp`, `test_release_validation.py`, `test_ixview.cpp`, `large_images_ixview.cpp`, `ui_quality_ixview.cpp` | New regression coverage, encoder-aware references and UI captures |
| `.github/workflows/desktop.yml`, `release-windows.yml` | Cache invalidation for native source lock changes |
| `README.md`, `CHANGELOG.md`, `PROJECT_STATUS.md`, `docs/ARCHITECTURE.md`, `SETTINGS.md`, `PORTABLE.md`, `PLATFORMS.md`, `THIRD_PARTY.md`, `PERFORMANCE.md`, `FORMAT_COMPATIBILITY.md`, `RELEASING.md`, this report | Features, build/runtime contracts, licenses and evidence |

README photos and image tags are retained. Personal portable profiles and
installed tools must remain unchanged across candidate deployment.
