# UI/UX consistency and readability review — 29 October 2026

Historical 0.4.8 checkpoint. Version 0.5.0 subsequently removes the file journal and file-operation Undo; current behavior is documented in ARCHITECTURE.md and CHANGELOG.md.

Scope: current üxzView 0.4.8 working tree, C++20/Qt Widgets, Windows 11. This pass continues the architecture/resource audit. It preserves the existing visual character and the original thin vector icons; no styling library or global stylesheet was added.

## Findings and changes

| Priority | Finding | Change / evidence |
| --- | --- | --- |
| P1 | Hovering commands replaced the image information with temporary status tips. | MainWindow consumes StatusTip events. Image/selection values remain visible; control tooltips and accessible command descriptions remain available. A loaded-image regression checks text, visibility and absence of status-message transitions. |
| P2 | The histogram used a fixed 768 × 280 black raster and a hardcoded German axis caption. Its minimum width exceeded small windows; the raster did not adapt to theme or DPI. | A small private QWidget paints vector curves in the current palette. Layout/font metrics place the axes; RGB/channel selection plus solid/dashed/dotted curves avoid relying on color alone. The worker still scans one row at a time and cancellation joins it. No chart work occurs outside this dialog. |
| P2 | The function-extension footer forced widths beyond 720 logical pixels in some languages. | Selection actions occupy one row; local installation and standard Close/Cancel controls occupy the footer. Empty status text consumes no row. Qt continues to order standard buttons. |
| P2 | Disabled text was unnecessarily faint: about 2.49:1 on light selected backgrounds and 3.02:1 on dark selected backgrounds. | Central palette roles now meet the project's voluntary 4.5:1 text goal for tested Base/Window/Button/Highlight pairs, including disabled roles. Disabled text remains distinct. |
| P2 | Image-information values used AutoText and only mouse selection. Literal filenames containing markup could render incorrectly. Several edit controls had no accessible name. | Information values use PlainText and support keyboard selection. Metadata, zoom, search, pipeline controls and extension fields receive names or label buddies using existing translations. |
| P3 | Standalone fullscreen-information forms used different default spacing. Repeated shared dialog metrics had no named constants. | Reuse dialogLayout/dialogFormLayout and the shared 16/12/16/8 logical-pixel spacing constants. Existing special layouts remain local. |

The contrast target is WCAG-oriented, not a conformance claim. [WCAG 2.2 SC 1.4.3](https://www.w3.org/WAI/WCAG22/Understanding/contrast-minimum.html) exempts inactive controls; raising their contrast is a readability choice here. Palette checks cannot certify native titles, OS dialogs, focus visibility, antialiasing or every rendered state.

## Coverage and intentional differences

| Surface | Review and verification |
| --- | --- |
| Main window, menu bar, toolbar, status row, previews | Existing palette, vector icon cache, checkmark and navigation tests retained. Added status-tip regression. The one-pixel status separator and right-hand command search remain. |
| Image display/fullscreen | Image backgrounds stay separately configurable and black by default. Theme changes must not recolor pixels, reset zoom or rescan directories. Existing fullscreen/fit tests retained. |
| Resize, rotation, colors, Settings | Existing themed layout, input synchronization, live-language and four-scale tests retained. Settings keep vertical categories. Scrollable edit bodies retain their fixed standard-button footer. |
| Histogram and image information | New targeted small-window, literal-text and accessibility tests; rendered snapshots in tests/output/ui-audit. |
| Batch processing | Preserve the single vertical input/chain/output order, separate Results page and direct Start. Label buddies/names improved; no new explanatory captions or preview/confirmation step. |
| Extensions | Both format and function tabs inspected; ten theme/language variants test the compact function-tab footer, geometry, Tab movement and Escape. Installation and tool-state flows remain unchanged. |
| File journal, progress, errors and confirmations | Journal is separate from the removed internal trash. Plain-text journal feedback and native-trash restoration hints prevent markup interpretation or misleading Undo. Existing crash/fault-injection tests retained. |
| Context menus and file pickers | Existing command/selection/keyboard wiring retained. Native file pickers remain OS-owned; no simulated redesign. Full native picker and screen-reader review remains open. |

Typography continues to use existing language/platform fonts, including Korean font handling. Icon artwork remains the established 1.25-unit outline set. Different image canvases, checkerboards, histograms and dialog bodies serve different content and need not share one background color. Standard buttons follow Qt platform conventions; manual grid labels now link to their fields, consistent with [QFormLayout's automatic buddies](https://doc.qt.io/qt-6/qformlayout.html#addRow-4).

## File-management behavior requested during the review

New Delete operations use QFile::moveToTrash(), which delegates to the [platform trash implementation](https://doc.qt.io/qt-6/qfile.html#moveToTrash). They do not hash/read the whole image first or keep a second internal copy. Failure is reported with no fallback to permanent deletion. Restoration belongs to the operating system. Native-trash entries cannot be undone by moving native trash files directly; a generic Undo does not unexpectedly act on an earlier operation instead. A crash between OS acceptance and journal completion is explicitly ambiguous and never replays deletion.

The former internal trash is no longer used for deletion. Journal records remain history; permanently removed legacy trash files cannot be recovered through it. Legacy-file cleanup requires the owner's explicit instruction. Recovery backups for reversible copy/batch replacement operations now use file-journal/backups. Existing record readers remain compatible with historical paths; this is not a new internal deletion bin.

Folder navigation already filters by enabled image-format extensions. Video/text/unknown files are skipped by default, without extra settings or repeated notices. This is filename filtering rather than decoding every directory entry. A corrupt file with a supported image extension, or an explicitly opened unreadable file, still reports an error. The mixed-directory regression includes uppercase image suffixes, non-images, a directory with an image-like name and a corrupt PNG-named file.

## Verification

Before correction, the targeted UI suite reproduced 18 failing cases (8 passed): fixed histogram width, footer overflow, missing accessible names, AutoText values and disabled-palette contrast. The corrected build passes all 423 Qt cases and 45 Python tests. All 33 validator blocks pass, including native Windows widget tests at 100/125/150/200 percent scaling. The portable runtime also passes all 423 Qt cases without SDK paths, including all five OCR models, both recognition engines and relocated-profile startup. Deployment resolves all imports for 136 PE modules; 65 dependency-source URLs were refreshed with unchanged SDK/runtime hashes.

Evidence paths are build/ui-audit-repro-before.txt, build/ui-quality-test-results.txt, build/ui-audit-validation-summary.json, build/ui-audit-portable.log and tests/output/ui-audit/*.png. Captures are test artifacts and are excluded from releases. The test output path is explicit, so portable verification does not create a tests directory in the deployed application.

A standalone Windows smoke check also moved a disposable file to the real system trash from a background thread (accepted=true, source_removed=true); evidence: build/ui-system-trash-smoke.json. No existing images were used.

The new UI suite is registered in CTest, the Windows four-scale validator and SDK-independent portable verification. Shared code uses the Qt 6.8 baseline API; no newer supportsMoveToTrash() API is required.

## Limits and remaining work

The computer-use helper failed to launch the application twice with GetCursorPos / access denied (0x80070005). Native Qt test processes and their rendered widgets are usable, but interactive OS-input testing through that helper was unavailable. No claim is made for screen readers, Windows High Contrast, actual mixed-monitor DPI transitions or platform-native pickers. Native Windows 10, Linux, Intel macOS and Apple Silicon macOS validation remain outstanding. French/Japanese/Korean layout coverage is not linguistic review.

No first-image or browsing speedup is inferred from this visual pass. It adds no image/navigation timers, decoding, theme polling or eager extension loading. Remaining architecture/RAM and color-management risks are listed in ARCHITECTURE_REVIEW_2026-10-28.md.

## File-operation follow-up

A subsequent rename regression demonstrated unnecessary neighbor decoding:
the old global cache clear raised its decode count from 1 to 2. Direct file
operations now invalidate source/target paths selectively. The neighbor's count
stays 1; unaffected previews and APNG composition sessions also remain retained.
Cache keys hold paths separately from revision strings, so delimiter characters
in Unix filenames are unambiguous and invalidation preserves LRU order. An
epoch still prevents an older in-flight read from repopulating evicted entries.

FileJournal now returns an explicit `applied` flag independently of error text.
The window updates its document/path and folder listing for an applied file
change before displaying a journal-completion warning. Nine injected cases
cover copy, move and native trash with success, failed intent persistence and
failed completion persistence. Renaming/moving also avoids a redundant scan of
the old directory before updating the document path.

Copy/move also verify the source around hashing and after intent persistence.
Four peer-write cases change the source during intent or the target during
completion. The former stops before the file change; the latter yields a
target revision mismatch, allowing the window to reconcile older displayed
pixels. This narrows external-write races; it is not an atomic lock against
other applications.

Targeted evidence: build/file-ops-before.txt, file-ops-after.txt,
file-ops-journal.txt, file-ops-maintenance.txt and file-ops-apng.txt.
Complete follow-up verification is recorded separately in
build/file-ops-validation-summary.json and build/file-ops-portable.log.
Global invalidation in broader journal/Undo/batch flows remains separate work;
no whole-application or cold-start speedup is claimed.

Final follow-up validation: **438 Qt cases** pass in both the development build
and SDK-independent portable runtime, with no skips. All **45 Python tests**
and **33 validator blocks** pass, including native Windows widget checks at
100/125/150/200 percent scaling. The final build introduces no compiler warnings.
The portable run also verifies real recognition engines, all five OCR models
and relocated storage/tool startup. Deployment resolves 136 PE modules; the
65 source-package URLs were refreshed without changes to SDK/runtime hashes.
Native Windows 10/Linux/macOS validation and atomic protection against
non-cooperating external writers remain outside this evidence.
