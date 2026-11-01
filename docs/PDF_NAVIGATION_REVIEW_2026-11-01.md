# PDF and unreadable-folder handling

1 November 2026; version remains 0.5.0.

## Behavior

PDF pages open through the existing image view, with previous/next-page toolbar
actions and Ctrl+PageUp/PageDown. Different page sizes are respected. Pages are
rasterized at 144 DPI on white, with title/author metadata and a bounded pixel
allocation. Save opens an image export dialog rather than overwriting the PDF.
Thumbnail requests render directly to their bounded target size.

Options → Settings → Navigation → Unreadable files offers three choices:

- Skip (default): index supported suffixes, then skip failed image decodes.
- Message with filename: index all regular files, show full path and error.
- Black display: index all regular files, paint black without a popup and keep
  wheel, keyboard and fullscreen mouse navigation active.

Unknown file types are therefore absent by default and visible in the latter
two modes. Hidden files retain the directory scanner's existing policy.
Black entries have no image pixels and cannot be edited or saved as images.
Explicit opens always report a named error and retain the previous document and
edits on failure. A failed skip pass also retains that document. Failed paths
are not permanently blacklisted; repaired files can be opened on the next pass.

## Implementation

`ixpdf` links Qt PDF/PDFium separately from the executable/core/UI. Each worker
owns its QPdfDocument; Qt serializes PDFium internally. Format/filter enumeration
and ordinary PNG reads leave Qt6Pdf.dll and ixpdf.dll unloaded in the fresh-process
Windows regression. A PDF page or PDF thumbnail loads the module on demand.
This is a loading assertion, not a cold-start timing benchmark.

The generic Qt qpdf handler is excluded because full capability enumeration
otherwise loads PDFium and Qt Network early. Build executables use a private
SDK plugin copy; SDK files are unchanged. CTest overrides SDK plugin paths.
Deployment excludes the eager handler on Windows and removes it from the native
Linux/macOS stage. macOS installation removes the build-only qt.conf before Qt
generates its relocatable bundle configuration.

FolderTraversal owns an implicitly shared snapshot and bounded cursor. Failures
advance without a directory rescan or growing visited set. Deferred explicit opens
retain priority over automatic skips. Settings default to Skip and take effect
after OK without restarting. Existing discard/save preferences remain applicable.

## Build and distribution

The pinned Windows SDK adds Qt PDF 6.11.2. The native Qt 6.8.3 jobs request qtpdf;
official Linux x64 and universal macOS archives and aqt 3.3.0 extension discovery
were checked. Full builds require Qt PDF; reduced builds can set IX_ENABLE_PDF=OFF
and leave IX_REQUIRE_FULL_FEATURES off. See [platform instructions](PLATFORMS.md).

Qt6Pdf.dll is 8,920,219 bytes and ixpdf.dll is 58,301 bytes.
Qt PDF Quick/Widgets and browser processes are not bundled. The viewer budget
increases from 160 to 170 MB; the 25 MB tools budget is unchanged. Runtime DLLs
are mapped to their pinned packages/source URLs, including Qt PDF/PDFium;
license texts ship with the runtime. README images remain unchanged.

## Verification

All 8 Qt suites pass in the build and portable runtime: 549 cases, no skips.
Counts: 200 functional, 154 workflow, 76 quality, 12 storage, 29 file-operation,
17 extension, 15 maintenance and 46 UI quality. All 46 Python tests pass.
Tests cover both navigation directions in all three modes, bounded all-bad
folders, repaired files, literal filenames, retained edits, settings Cancel/OK,
black-entry wheel/key/fullscreen clicks and Enter exit, PDF page sizes/colors/white margins,
bounded thumbnails, parallel documents, malformed/oversized pages, PNG export
and unchanged PDF source hashes. A CMake fixture verifies that deployment removes
only the named eager PDF handlers and preserves other files.

Targeted native Windows checks pass at 100/125/150/200 percent scaling, including
the new settings row and black-entry fullscreen clicks. Portable OCR/QR, five
languages, profile relocation and all three lazy-module exports pass without SDK
paths. The native deployment CMake configuration also generates successfully.
Evidence: build/review-pdf-fullscreen-final-validation-summary.json,
build/pdf-navigation-runtime-summary.json and their referenced reports.

Build and runtime EXEs match. Windows metadata is 0.5.0.0; SHA-256:

`efbb8f5c0d0bb4eddd1b89e761fc0ffbdb313f017ec214ef809cc1c8868b4116`

Local Defender scans explicitly report no threats. No upload or vendor submission
was performed. The build remains unsigned.

## Limits

PDF tests use self-authored fixtures, not a representative production PDF corpus.
PDF is a raster reading feature: no text selection/search, form editing, PDF
writing or password dialog. Parser allocations, caches and concurrent work have
costs beyond the page pixel guard; this is not a total-process RAM limit or a
parser sandbox. Rendering/ICC fidelity across real documents needs broader tests.
Native Windows 10, Linux and macOS were unavailable; their PDF builds/runtime
still need CI and on-device validation. Core APIs target Qt 6.8 and newer.

Reference: [Qt PDF](https://doc.qt.io/qt-6.8/qtpdf-index.html) and
[QPdfDocument](https://doc.qt.io/qt-6.8/qpdfdocument.html).
