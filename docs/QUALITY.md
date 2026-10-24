# Quality checks

üxzView 0.4.4 has five Qt test suites. On Windows 11, the development build and the portable runtime each pass **154 functional checks, 13 quality checks, 12 storage checks, 17 extension checks and 11 workflow checks, with no skips**. Twenty-four Python tests cover SDK extraction, translations, Windows registration generation and release artifact validation. These counts include Qt's setup/cleanup checks and data-driven cases; they are not a count of independent features.

`tests/workflow_ixview.cpp` covers superseded modal opens, queued folder navigation, EXIF-rotated preview detail, random rounds after directory changes, external rewrites during Rename, and exact export pixels/profile/DPI with failed-save preservation. The first, rotated-preview and rename cases were reproduced against the previous implementation. See [the follow-up review](REVIEW_2026-10-24.md).

The 0.4.4 workflow additions cover restarting cancelled previews without navigation, manual selection during random slideshows, and reopening a cached RAW without publishing a lower-resolution preview. All three failed against 0.4.3 before correction. The complete 0.4.4 validation includes the refined 19-icon artwork and existing toolbar/DPI regressions; the earlier artwork-only run remains recorded in `build/icon-refinement-*.txt`. See [the maintenance review](REVIEW_0.4.4.md).

`tests/extensions_ixview.cpp` verifies streamed package integrity, rejected paths/platforms/catalog mismatches, cancellation, activation, custom-folder migration and portable import. Actual standalone QR/Code 39 and English OCR processes run from installed packs, including a selected image area, five installed language models, manager installation/deactivation/removal and temporary-file cleanup. Removal checks cover retained versions, cancelled operations, corrupt state, multiple simultaneous leases, selected OCR languages and reinstallation; profile copies omit transient tool locks. A real QR fixture is selected with the mouse and read through its right-click menu, and right-drag keeps moving the frame. The tools also run without SDK paths in PATH. The deployed folder includes both engines and five models under app-local tools; clean-room checks launch the QR engine and each OCR language, then move the app and check its default tool path without a duplicated portable copy. Release tests preserve installed activation/removal state and publish a separate verified snapshot rather than personal tools. Invoking QR recognition without its engine opens the manager on the correct function pack, then resumes into recognition after installation; result-action availability and cancellation are checked. These establish integration rather than broad recognition accuracy. Native Linux/macOS tool runs and Japanese/Korean OCR accuracy remain open.

`tests/storage_ixview.cpp` adds isolated portable-profile import, cancellation, typed INI settings, backend locations, relocation with ratings/file undo, external-path boundaries and corrupt/unwritable input checks. The real deployed executable is also started in a separate portable folder, moved and restarted; its local extension preferences must be honored. Release tests preserve user profiles during deployment and exclude them from binary/source archives.

Fullscreen information tests cover menu placement, Cancel/OK and Settings persistence, selected image/file/EXIF fields, missing metadata, all four corners, small viewports, current-image refresh and removal on returning to window mode. The image pixels and modified state remain unchanged. Branding checks cover the üxzView window title, UTF-8 version output, ASCII package/executable names and generated per-user Windows registration; legacy storage identifiers remain in use.

Fullscreen exit keeps a temporary snapshot window above the owner through queued Qt layout/paint work, a compositor fence for the completed frame and another queued geometry check, without activation or input capture. The initial cover is submitted with GdiFlush and a single DwmFlush presentation fence before native owner resizing. GdiFlush alone left a compositor race and was insufficient. The owner’s original DWM transition setting is restored while covered, before its final presentation fence. Unchanged geometry does not force repeated repaints or refresh-interval waits. Native tests require the cover’s successful compositor fence before exit resizes and require this cover during those resizes and after the synchronous toggle returns, then require its removal. Rapid toggles and destroying an owner with a pending cover check cleanup. A regression also changes/restores central-layout margins over two queued turns after exit and requires protection while that late layout work is pending. Dedicated native runs at 100/125/150/200 percent scaling exercise 20,003-pixel-wide images, the actual fitted bounds, unchanged/aborted zoom input, previews, fullscreen return, and real folder navigation by keyboard and wheel. These structural tests do not establish visually flicker-free presentation on every GPU/driver. Exact pixel-art resizing checks every output pixel at 2×/3×/4× in 8-bit and 16-bit formats, including partial transparency; the explicit pixel-replication UI method disables resampling/sharpening options.

Settings checks exercise numeric aspect-ratio rows, the 32-row limit, row removal/focus, normalized duplicate ratios and unchanged Cancel/save behavior. Existing profile strings remain compatible.

`tests/test_ixview.cpp` covers processing, document state, UI commands, file undo, batch plans and codec round trips. It uses generated RGB/RGBA, GIF/APNG, PSD, SVG and multipage TIFF fixtures. PNG has exact 16-bit pixel, alpha, ICC, DPI and text checks; JPEG has EXIF-orientation and lossless coefficient-transform checks. The runtime matrix has 94 format keys and 32 synthetic encoder round trips. Recognition and a small round trip do not establish support for every variant of a format.

Selection repaint checks compare complete before/after pixels with actual damaged regions at fractional zoom, including clipped/tiny frames, removal and 8-/16-bit sources. Metadata range reads cover read-only Unicode JPEGs with trailing data and PNGs across the 64-KiB buffer boundary; EXIF, IPTC and XMP remain readable. The Exiv2 0.27 compatibility path and other desktop operating systems have not been newly tested here.

Windows extension tests also force a directory sharing lock during publication of prepared data. They check recovery after release, bounded failure for a retained lock, cancellation and preservation of an occupied destination. Normal successful commits do not wait; only failed Windows renames retry, for at most 200 ms.

## PSD coverage

The shipped KImageFormats handler is tested with synthetic flattened RGB composites at 8 and 16 bits per channel, both uncompressed and PackBits/RLE-compressed, including rejection of incomplete image data. These checks do not establish complete support for layers, PSB, masks, Photoshop-specific blend modes, CMYK or embedded profiles. The viewer displays the stored composite rather than editing Photoshop layers.

## Real camera files

`tests/quality_ixview.cpp` tests five pinned CC0 files from [raw.pixls.us](https://raw.pixls.us/). URLs, SHA-256 hashes, camera modes and expected output dimensions are in `tests/fixtures/raw-manifest.json`.

| Camera | Format | Expected decoded dimensions |
| --- | --- | --- |
| Canon EOS 7D | CR2 | 5202 × 3464 |
| Nikon D7000 | NEF, compressed 14-bit | 4948 × 3280 |
| Sony ILCE-6000 | ARW, compressed 12-bit | 6024 × 4024 |
| Pentax K10D | PEF, compressed 12-bit | 3896 × 2616 |
| Pentax K10D | DNG, uncompressed 12-bit | 3896 × 2616 |

Each case verifies the file hash, exact dimensions, 16-bit output, a valid ICC profile, nonuniform image content and the embedded thumbnail's bounds/aspect ratio. It exports a reduced PNG, checks exact 16-bit pixel and profile preservation, and verifies that the original RAW has not changed. Dimensions were cross-checked with the locked SDK's LibRaw `raw-identify`; this is not an independent demosaicing reference.

The files are downloaded only on request and stay under `build/quality-fixtures`, outside the source and runtime archives. Approximately 96 MB is needed. Ordinary offline tests report missing RAW fixtures as explicit skips; release preparation sets `IX_REQUIRE_RAW=1` so missing files fail the run.

Exiv2 reports unsupported Nikon MakerNotes and XMP decoding warnings for some fixtures. Successful pixel export does **not** imply complete camera metadata preservation. CR3, RAF, ORF, RW2, sensor-specific color accuracy, damaged RAWs and further compression modes still need coverage.

## Color and HDR references

Four 16-bit RGBA sample sets in linear sRGB, Adobe RGB, Display P3 and ProPhoto RGB are independently converted to sRGB using LittleCMS and compared with Qt's conversion. Maximum observed RGB differences are 28, 43, 24 and 65 code values out of 65535; the test allows 96 and requires exact alpha. The checks include PNG ICC transport and the main window with color management enabled and disabled, while retaining the original document pixels/profile.

LittleCMS development files are optional for source builds. If absent, the four cases explicitly skip. They are installed in the Windows SDK and the prepared Unix CI jobs.

An OpenEXR round trip checks linear floating-point samples at −0.125, 1.5 and 4.0 with alpha 0.5, allowing a 0.005 absolute error. Export warnings must identify HDR loss, including for half-float input. A HEIC color/alpha round trip checks the replacement encoder; the portable package's mean RGB error was 0.916/255 and alpha error was zero for this synthetic fixture. These samples are not an HDR tone-mapping or photographic encoder-quality benchmark.

The viewer currently converts tagged images to sRGB for display. Calibrated monitor profiles, multiple monitors, HDR desktop output and visual perceptual accuracy remain unverified.

## State and storage regressions

- Portable-mode Settings Cancel and failed imports retain the original preferences. Successful import stays inactive until restart; disabling preserves both profiles.
- Repeated fullscreen transitions retain window identity, visibility, maximized state, geometry, zoom and enabled updates, with the neighbor strip both shown and hidden. Toolbar/thumbnail icons and preview widgets are retained; browsing in fullscreen refreshes the neighbor strip on return. Enter autorepeat is disabled. On Windows a scoped native event filter suppresses intermediate frame/background paints and adds `SWP_NOREDRAW` to geometry changes; tests check the resulting native resize flags. The final client and frame are repainted together before restoring normal DWM animation behavior. This automated check does not prove absence of every compositor/GPU-specific visual artifact. The mechanisms use [DWM transition control](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute) and [native window positioning flags](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos).

- Browsing between images retains toolbar enabled states without transient dimming. Mutating commands remain blocked until loading finishes. Settings categories fit vertically, support keyboard selection and select the corresponding scrollable page. Preview backgrounds are pixel-checked against dark/light image colors and inherited transparency; switching colors retains button/icon identity.

- A complete 65,536-value Grayscale16 ramp retains its samples through PNG, identity color adjustment/resampling, normal/inverted selection composition, zero-margin canvas, transparent watermark and quarter-turn rotation. The test also checks unchanged samples outside an edited selection and extended float values in identity selection/canvas. This does not establish HDR preservation in every processing filter.
- Animation resume tests cover Cancel, Discard and Save, paused display refresh and restarting a completed one-shot animation.
- Real file-undo and reviewed pipeline dialogs replace the active file: clean documents reload and clear history; dirty documents retain pixels/history with an explicit disk-change warning.
- Undo/Redo display work commits history only after the worker succeeds; the GUI event loop continues during display sharpening.
- APNG sequences of 20/40/80 frames parse once and compose exactly that many frames at document-cache budgets of 0/16/192 MiB. Returned frames stay immutable; backward seek, same-size/same-mtime replacement, cache clearing and over-budget eviction are checked. Existing blend/disposal/CRC checks remain.

- A slideshow cannot replace the document while a save dialog or file transaction is open. External open requests wait until the transaction ends.
- Exporting a GIF frame to PNG resets page and animation state, including earlier undo snapshots that now refer to the saved PNG.
- A changed watermark invalidates a reviewed batch plan; output cannot overwrite the watermark resource.
- Thumbnail loading reaches beyond the first 200 entries and responds to viewport changes.
- Decoded documents retain their detected format even when the Qt handler clears it at the end of reading. The installed PNG probe checks the identifier as well as dimensions.
- Journal migration accepts a legacy history larger than the old 32 MiB read limit. Updating one operation leaves other entry files unchanged. Corrupt storage is rejected before changing a user file.

Whole-image fitting is exercised across multiple canvas sizes, aspect ratios and dimensions that expose floating-point rounding. Tests zoom in, fit back to the viewport, require zero automatic scroll ranges and full image bounds, then send real wheel events. A separate check confirms that a genuinely zoomed image still scrolls rather than changing files.

Navigation tests crop and convert to grayscale before moving by keyboard and wheel. They check default disposal without a save question, unchanged original files, cleared undo history, retention after a failed load, and the unchanged close-window question. The optional save question is persisted through Settings and tested with Cancel and Discard. Edited animation frames also discard their undo history when changing pages.

The additional review covers opening a duplicate without leaving blocking dialogs, retaining ordinary window geometry/fit after fullscreen, offering PNG Save as for decode-only sources, and retaining JPEG commands after saving an uppercase `.JPG`. Tagged image watermarks are converted into the destination image's profile before composition; the source watermark remains unchanged. Release packaging rejects missing or stale üxzView executables/modules relative to the current build. This last check does not replace rebuilding after source changes; `prepare_release.ps1` performs that build.

The journal still needs manual inspection of `pending`/`undoPending` entries after a process crash; automatic recovery is not implemented. Its history UI still reads the full entry index, and the metadata catalog remains a bounded JSON snapshot rather than a database.

## Reproduce

```powershell
python -m unittest discover -s tests -p "test_*.py" -v
python scripts/fetch_quality_fixtures.py
$env:IX_REQUIRE_RAW='1'
./scripts/build.ps1 -Test
python scripts/deploy.py
python scripts/verify_portable.py
```

`build/test-results.*` and `build/quality-test-results.*` contain the development results. `verify_portable.py` runs all five suites with only Windows system paths in `PATH`, checks the installed application's version and actual PNG decoding, then removes the temporary test executables and Qt Test DLL. Reports are `build/portable-test-results.*`, `build/portable-{quality,storage,extension,workflow}-test-results.*` and `build/portable-image-probe.json`.

Thirty-four targeted behaviors also passed with the native Windows platform plugin on Windows 11: fullscreen fit and closing, saving during slideshow, animation export, file opening during save, keyboard folder navigation, duplicate-result opening, saving decode-only formats, uppercase JPEG extensions, tagged watermark composition, fitted-image wheel navigation, default edit disposal, optional save confirmation, aspect-ratio crop/undo and fullscreen mouse navigation. The run also includes the grayscale, animation, file-replacement, worker-history and APNG regressions above, plus stable toolbar states, keyboard-accessible settings categories, preference Cancel/OK/persistence and preview background rendering without thumbnail replacement. This automated run does not replace manual DPI, printing, multi-monitor and drag-and-drop checks. The restricted session initially denied clipboard access; the successful run used a normal desktop context outside that restriction.

Native Windows 10, Linux and macOS results remain outstanding. The separate Windows 10 workflow requires an actual Windows 10 desktop runner and refuses Windows Server/11. Unix CI also decodes a PNG using the installed application. See [platforms](PLATFORMS.md); prepared workflows are not successful test runs.

Aspect-ratio crop checks cover preset/custom validation, Settings Cancel/OK/persistence, dragging in all directions to image edges, redraw inside an existing frame, constrained keyboard resizing, moving, exact crop pixels/ICC and undo. Fullscreen mouse checks cover previous/next clicks, background clicks, selection drags, right-drag movement, modifier clicks, return to window editing and double-click entry.

Streaming directory checks compare natural/alphabetical/date/size sorting in both directions against a reference list, including Unicode, case, hidden/type filters, latest-file selection and cancelled/missing folders. Neighbor-strip checks retain widget/icon identity across navigation, verify click targets, refresh changed/deleted files and reset a new directory snapshot. Native Windows checks include the neighbor strip.
