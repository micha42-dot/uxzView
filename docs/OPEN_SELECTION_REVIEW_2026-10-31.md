# Image opening and outside selection — 31 October 2026

## Opening latency

`openFilter()` previously called full reader/writer capability discovery while
preparing the file picker. The extension list already had a metadata-only path,
but the individual format groups bypassed it. A synthetic codec regression
detects unnecessary capability queries; the benchmark also checks five actual
Windows plugin modules in fresh processes.

| Filter preparation, fresh process | Before | After |
| --- | --- | --- |
| First call, three runs (ms) | 346.014 / 274.945 / 133.449 | 75.498 / 73.524 / 76.373 |
| Median (ms) | 274.945 | 75.498 |
| Tracked heavy plugins mapped by the call | 5 | 0 |

The median falls by about 72.5% in these samples. The tracked plugins are RAW,
HEIF, AVIF, OpenEXR and JPEG XL. This measures filter preparation on a warm
filesystem, not total native dialog time, directory enumeration, cold storage or
an IrfanView comparison. Evidence: `build/open-filter-before-*.json` and
`build/open-filter-after-*.json`.

The opening filter now groups reader extensions without writer discovery.
Image-file and all-file labels follow the interface language. Explicit export
and format-detail operations retain full capability discovery.

## Dialog presentation

The shared `image_file_dialog` helper prepares opening, comparison and slideshow
pickers before showing them: compact visible filter names, generic directory
icons, existing-file mode and bounded 800×520 widget geometry. Actual wildcard
patterns remain in the filter; native dialogs remain enabled.

Qt documents the [filter-detail and directory-icon options](https://doc.qt.io/qt-6.8/qfiledialog.html#Option-enum).
The directory-icon option avoids custom folder-icon lookup in the Qt provider;
it is not a promise to disable Windows Shell thumbnails. The
[shipped Windows helper](https://raw.githubusercontent.com/qt/qtbase/v6.11.2/src/plugins/platforms/windows/qwindowsdialoghelpers.cpp)
passes the hidden-details flag and filter list to the native dialog before it
runs. Native window geometry remains OS controlled.

The Qt fallback tests inspect the first Show event, verify no subsequent visible
resize, check compact labels and the initial directory, and exercise selection
and Cancel with an edited image. They also check that the code does not force
the non-native option. These are not native Shell presentation tests.

Live Computer Use was attempted twice and failed with
`GetCursorPos failed: Zugriff verweigert (0x80070005)`; no target viewer window
was exposed. The native Windows small-to-large transition and comparison with
IrfanView could therefore not be visually confirmed in this session.

## Selection from the background

Left dragging can start outside the image. The anchor/end are clamped to image
pixels, and the viewport drag must intersect the rendered image before a frame
appears. Existing selection magnification requires an actual inside-image press.
Free and fixed-ratio selections stay within the image; ordinary background clicks
clear selection as before, and fullscreen click navigation remains intact.

Tests cover eight sides/corners with free and 16:9 selection, fractional zooms,
background-only drags, unchanged image pixels and exact last-column/row bounds.
The first implementation exposed five reverse-edge failures; explicitly inclusive
endpoint construction corrects those missing pixels. Existing ratio, magnifier,
fullscreen and damage-region checks remain in the targeted run.

## Evidence and limits

Before product changes, 17 of the 18 new main-suite cases failed; the background
case already passed. Setup/cleanup add two passes to that report. Evidence:
`build/open-selection-before.txt`. The subsequent reverse-edge failure is retained
in `build/open-selection-initial-after.txt`.

Current logs use `build/open-selection-*`, `build/open-dialog-*` and
`build/open-filter-*`. The corrected main target passes 23 cases including
existing related checks; the fallback dialog target passes four including setup
and cleanup. Native Windows widget runs cover the same selection behavior and
the forced Qt fallback. Native Windows 10/Linux/macOS execution and the Windows
Shell visual transition remain open.

All eight suites pass 465 Qt cases without skips in the development build and
SDK-independent portable runtime: 191 functional, 128 workflow, 43 quality,
12 storage, 21 file-operation, 17 extension, 15 maintenance and 38 UI quality.
All 45 Python tests pass. Portable QR, five OCR languages and relocated-profile
startup pass. The compiler emits no warnings. The complete 33-block DPI matrix
recorded in earlier reviews was not rerun for this change.
