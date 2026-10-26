# Änderungen

## Unreleased

- Reuse cached, unchanged single-page PNG/BMP originals for neighbor previews without decoding the file again. Retain decoder paths for JPEG, RAW, animations and multiple pages; recheck cancellation and source revision after scaling.
- Fix the remaining Exiv2 0.27 thumbnail assertion that prevents Linux tests from compiling. Identify the save-warning test by content/buttons rather than macOS-ignored message titles, including a titleless regression.
- Retain native configure/compiler logs as CI artifacts even when compilation fails, and install both existing README images with native documentation.

- Keep filesystem timestamps with the loaded document, refresh source state on worker-side save/rename completion and retain it through Undo. Zoom/selection status updates no longer query the file's last-write time.
- Add bounded first-open diagnostic runs with retained partial reports/checkpoints, stale-result rejection and five subprocess regressions. Benchmark windows no longer automatically take focus.

- Resolve folder image-format keys on the existing scan worker so plugin metadata discovery no longer blocks the first image paint. Coalesce recent-file/directory settings updates and avoid unchanged writes.
- Add a repeatable first-open benchmark for an already-running window. Keep both README image assets in public exports and installed documentation.

- Use self-cleaning platform temporary directories for Python release fixtures, including fresh CI checkouts without `tests/output`.
- Accept both Exiv2 0.27 `auto_ptr` and newer `unique_ptr` results without changing metadata handling.
- Isolate Save-as fixtures, submit file dialogs once and fail promptly on unexpected save dialogs. Keep the saved destination through Undo and retain user overwrite warnings.
- Observe the modeless macOS About window in the branding test. Print and upload complete reports for all five Qt suites and run Python checks on every desktop CI platform.

## 0.4.8

- Keep command-search rows instead of rebuilding the model on every keystroke. Track action text/enabled state and remove destroyed actions immediately.
- Prevent an empty search from replaying a cancelled/completed command, skip unavailable default matches and guard action/widget lifetime during invocation.
- Show only stored thumbnail ratings/color marks, translate color names and replace the duplicate-dialog instruction with an Open button. Remove redundant crop/status/preview copy and unify command-search labels.
- Reuse the current-preview marker and keep image placeholders small at thumbnail sizes, with the existing palette/DPI-aware icon cache.
- Keep catalog progress visible until the worker finishes; cancelled or failed indexing no longer reports success. Folder filtering stops checking terms after its first mismatch.

## 0.4.7

- Cancel superseded folder scans and keep only the newest pending request; reject cancelled results before they reach the window.
- Move directory-job ownership out of MainWindow into a separate, low-priority service with bounded concurrency and explicit shutdown. Test cancellation, coalescing, reentrant consumers, destruction, failure recovery and main-window integration.
- Keep the reusable GitHub Updater in a separate project. Share the public-source selection rules between source archives and the external updater through `.github/github-updater.json`.

## 0.4.6

- Check save destinations before encoding and again before commit; retain the main-window target revision across output warnings and preserve intervening rewrites, creations or deletions.
- Reject unknown file-journal undo operations before moving files or changing operation state.
- Recheck batch inputs after decoding and before publishing output; allow configured codec registries in synchronous batch runs.
- Bound automatic DPI to the supported input range and reject invalid density conversions before image processing.
- Charge resize image buffers by method: nearest-neighbor has no separable-filter intermediate, while smooth/sharpen paths include overlapping conversions.
- Handle Space/Backspace directly in the image view as well as through window shortcuts, retaining Ctrl-modified five-image jumps.
- Extract unchanged native file revision logic into a shared core component. See `docs/REVIEW_0.4.6.md` for reproductions, allocation calculations and remaining limits.
- Refresh source availability for an unchanged, hash-verified runtime without restoring discarded SDK binary archives; retain the 24-hour release gate and test success/failure preservation.

## 0.4.5

- Guard screen-preset button IDs before indexing the resize dimension table.
- Make dimension shortcuts select the corresponding input mode and save newly added resize presets only after OK.
- Disable derived DPI and conflicting physical-unit inputs while automatic DPI is active.
- Honor interface decimal separators in resize, rotation and color editors; commit complete resize input before updating linked fields.
- Make fast rotation exclude antialiasing and inactive background options; use the common Cancel label.
- Share resize-filter names with batch processing and preserve the effective clamped filter when editing malformed profiles.
- Scope the version definition to the application target to avoid unrelated recompilation on future version bumps. See `docs/REVIEW_0.4.5.md` for evidence and remaining UI design work.

## 0.4.4

- Put Pixel replication / pixel art in the regular resize-filter dropdown. Disable smoothing-only options for that filter and restore them when another filter is selected.
- Use a Korean UI font for the Korean interface, including legacy dialogs, with a minimum 9-point size for Korean/Japanese dialog text. Apply language-aware fonts after startup language selection; no bundled font or new runtime dependency is required.
- Resume cancelled neighbor previews when the strip is shown again without navigating. Retain its existing buttons and refresh the cancelled requests.
- Check the full-image cache before requesting an embedded RAW preview. Cached originals no longer generate an unnecessary lower-resolution preview; uncached RAWs keep progressive loading.
- Skip the currently displayed image after manual browsing during random slideshows. Preserve shuffled rounds and begin a repeated round immediately when the remaining candidates were skipped or removed.
- Add regressions for all three reproduced cases and a controlled cached-RAW benchmark with exact document/display comparisons. Update release/version instructions to 0.4.4; native Windows 10/Linux/macOS validation remains open.

## 0.4.3

- Consolidate project status around the current implementation and remaining limits; correct five-suite release/platform instructions. Remove obsolete local probes, duplicate test builds and installed-SDK download archives while retaining active builds, source archives, current evidence and release assets.

- Refine all 19 toolbar/placeholder icons on one 24-unit grid, with consistent rounded strokes and clearer small-size silhouettes. Distinguish Undo from rotation, simplify Settings and give image information a separate badge; retain palette-aware disabled states and cached rendering.

- Recheck externally changed sources after file dialogs; cancel stale deferred opens when a newer file is chosen; preserve detail in EXIF-rotated thumbnails. Add a dedicated workflow regression suite to development, native and portable validation.
- Shuffle random slideshow rounds once instead of filtering the entire folder for each image. Stream exports directly into atomic staging files when metadata rewriting is unnecessary.

- Fix another reproducible fullscreen-exit race: the Qt cover could still be unpainted when the owner resized despite a successful compositor fence. Upload the snapshot to an opaque native Windows surface before showing it; add native coverage/cleanup/DPI checks and a screen-pixel assertion that reports when desktop readback is unavailable.

- Extract the image loading/display preparation pipeline from MainWindow into a worker-side core service with explicit options and preview delivery. Verify original precision/profile preservation, cached reads and failed pages/files independently of the window.

- Separate recognition input preparation from its dialog, with independent selection/alpha/cancellation checks. Document component ownership, threading, storage compatibility and maintenance workflows. Publish the native validation runner, share it with Windows 10 CI, and prepare bundled tool packs in normal Windows CI before deployment.

- Decode embedded RAW JPEG previews at the requested size; share the RAW suffix classification between codec reporting and preview loading. Keep thumbnail requests to two active decoders and drop cancelled queued work before creating futures.
- Use the function-tool folder's restart boundary for OCR/QR dialogs, independently of image-codec storage changes. Exclude personal app-local tool stores from Linux/macOS archives as well as Windows releases.

- Include ready-to-use QR/barcode and Tesseract OCR engines with five languages in the Windows package. Default function-tool storage to `tools` beside the app; retain custom stores and legacy layouts. Keep tools lazy and removable, avoid portable-profile duplication, and package verified defaults without local tool state.

- Shorten fullscreen exit: present the opaque cover before native resizing, repaint only after layout changes, restore DWM settings while covered, and use a final compositor fence plus queued geometry verification. Remove repeated refresh-interval waits and redundant final redraws. Add opt-in transition timing via `IX_PROFILE_TRANSITIONS`.

- Simplify settings and fullscreen information by removing explanatory paragraphs. Edit custom crop ratios through bounded width/height fields, with add/remove controls and the existing profile format. Show the storage location as a copyable path.
- Route missing recognition engines or OCR languages directly to their selected entry in the extension manager. Share the lazy module opener, remove internal OCR language codes from the list, and enable result actions only when text is available.

- Read QR/barcodes or text from a drawn selection through its right-click menu; preserve selection dragging, panning and fullscreen click navigation. The same commands remain under Image → Function extensions.
- Remove function extensions and individual OCR language packs, including retained older versions. Protect running tool jobs with per-process leases and omit those transient locks from profile copies. Simplify the extension and recognition dialogs.

- Keep the fullscreen-exit cover through queued Qt layout/painting and a final presentation/geometry check, including child geometry; cancel pending covers on rapid toggles and release them with the owner. Disable cover transitions and rounded corners on supported Windows versions.
- Preserve automatic image fitting when confirming an unchanged zoom display or cancelling a zoom edit. Fit against the actual viewport and restore fitting after the normal window layout. Exercise 20,003-pixel-wide images with previews, fullscreen exit, keyboard and wheel navigation.

- Use Michael Förtsch's supplied pixel logo for the application, Windows executable and macOS bundle; include scalable Linux artwork.
- Cache fitted display rasters lazily within a 32 MiB per-view limit, preserving source pixels and keeping overlays separate. Reuse native file revision snapshots instead of repeating filesystem queries.
- Reject files changed during decoding and prevent recursive portable imports when the application sits inside its custom extension folder. Reject junctions during profile/extension copying.
- Queue next/previous navigation during file and animation/page loading, including toolbar commands.
- Allow bounded cross-host HTTPS download redirects while blocking transport downgrades; handle extension/preparation worker exceptions and late cancellation consistently.
- Require exact-case tool entry paths and preserve disabled state when updating tools.
- Retry briefly blocked Windows staging-directory commits for up to 200 ms, respecting cancellation and preserving occupied destinations; exercise real sharing locks in the portable-import regression.
- Expand PSD composite checks to 8/16-bit RGB, raw/RLE compression and truncated inputs. See the critical review for remaining format/platform limits.


- Add separate offline QR/barcode and Tesseract OCR tool packs, selectable language packs, image/selection recognition and copy/save results. Keep engines separate from the viewer process and load them only on demand.
- Split the extension manager into image-format and function tabs, show verified package sizes, stream optional downloads and support cancellable extension-folder copying with a restart boundary. Include custom extension roots in first-time portable import.

- Group file undo and the journal/trash below file management, place classic batch renaming beside batch conversion, and keep Exit as the final File menu command.

- Rename the product to üxzView, with ASCII `uxzView` executable/package names. Retain the existing settings, portable profile and OS identifiers for compatibility.
- Add View → Show information (fullscreen), directly below the window display options. Choose a corner and individual file/image/EXIF fields; missing metadata is omitted and the overlay never changes image pixels.
- Keep the last fullscreen frame in a temporary non-activating, input-transparent window while restoring the native window, then release it after the final repaint/compositor flush.
- Name the exact nearest-neighbor resize method Pixel replication / pixel art; copy source pixel bytes directly, including transparent and 16-bit pixels, instead of relying on Qt's fast scaling path.

- Add optional portable storage with cancellable profile import, typed local INI settings, local profiles/catalog/extensions/journal/trash and relocation of references carried with the app. Preserve the original user profile; switch modes after restart.
- Preserve personal portable data during Windows deployment and exclude it from all release/source archives.
- Batch fullscreen layout changes and repaint before presentation; temporarily suppress Windows DWM transitions and disable Enter autorepeat.
- Suppress intermediate native frame/resize redraws during fullscreen changes. Restore window chrome and neighbor previews together without regenerating toolbar icons or reapplying unrelated settings; refresh neighbors after fullscreen browsing.

- Keep toolbar action states stable while loading images/pages to avoid repeated disabled-icon flashes; concurrent document edits remain guarded.
- Replace horizontal settings tabs with a vertical category list and scrollable pages.
- Add theme/image-background/transparent modes for the neighbor strip; reuse existing thumbnails when only appearance changes.

- Preserve 16-bit grayscale channel precision across processing, selection composition, rotation, canvas and watermark paths; detect precision loss when exporting grayscale to JPEG.
- Respect the save preference before resuming edited animations; retain pause across display refresh and allow completed one-shot playback to restart.
- Reconcile active documents after file undo and batch replacement; reload clean documents, preserve dirty edits and warn before overwriting a newer disk revision.
- Retain a revision-bound APNG composition session within a 64-MiB retained-state budget; sequential playback composes each new frame once while backward seeks and oversized sessions retain a replay fallback.
- Compute Undo/Redo display transformations in a worker and commit history only after successful completion.

- Read metadata through bounded, buffered Qt file I/O with Exiv2 0.28; retain the snapshot fallback for mapping-based parsers and Exiv2 0.27 compatibility. Redraw only changed selection outlines and suppress unchanged selection notifications.

- Refine the palette-aware outline icons, toolbar spacing and separators; align the optional command search to the right.

- Stream directory entries without preflighting access permissions for every file; keep natural/date/size ordering and cancellation. Reuse overlapping neighbor widgets and unchanged thumbnail icons while checking revisions in the background.
- Add aspect-ratio crop frames (16:9, 9:16, 1:1, 4:3, 3:2, 21:9) and validated custom ratios in Settings; crop and undo use original image pixels.
- Fullscreen left/right clicks browse previous/next images; dragging keeps selection and panning behavior.
- Clear stale scrollbar ranges when fitting the whole image and suppress floating-point pixel-boundary noise; wheel browsing works again after zooming back out.
- Discard unsaved edits on image/page changes by default, with an optional save question under Navigation. Closing still asks and failed loads retain the current edits.

- Additional review: open duplicate results immediately, preserve ordinary fit/geometry across fullscreen, offer PNG Save as for decode-only inputs, and retain JPEG actions after uppercase-extension saves.
- Convert tagged image watermarks into the target image's color space before composition.
- Reject missing or stale application executables/modules when packaging against the current build.

- Prevent slideshows, delayed captures and file-open events from replacing the document during save/file transactions; queue later open requests.
- Separate file commands and slideshow scheduling from the main window; reset page/animation state after single-image exports, including their undo history.
- Store file-journal operations separately with atomic updates; migrate older histories, including histories past the previous 32 MiB limit.
- Load thumbnails throughout large folders and after viewport resizing; apply catalog ratings to a whole selection in one write.
- Pin watermark contents to the reviewed batch plan and keep the decoded resource stable throughout processing.
- Build smaller libheif/libavif libraries with kvazaar/AOM instead of redundant x265/SVT-AV1/rav1e encoders; retain HEIF/HEIC, AVIF and other image-format backends.
- Warn about HDR export loss for half-float images as well as full-float images.
- Preserve detected format identifiers after decoding; some Qt handlers clear them at end of image.
- Add pinned CC0 camera fixtures and independent LittleCMS color-reference tests, plus explicit Windows 10 runner validation.

## 0.4.2

- Fullscreen shows the entire image, removes the view frame, hides thumbnails and restores the previous window zoom on exit.
- Robust percentile-based automatic correction instead of min/max levels that could be defeated by isolated extreme pixels.
- Thumbnail-strip visibility in its own section near the top of View.
- Five selectable interface languages: German, English, Korean, Japanese and French (restart to apply).
- Fullscreen and slideshow preferences, screenshot shortcut, delay and display selection.
- Per-user Windows Open with / Default apps registration scripts with inspectable .reg generation and unregistration.
- Short English README with build and default-app instructions for all three platforms.
- Fixed Save as / Undo destination and stale EXIF thumbnails when exporting a selection or resizing through classic batch.
- Batch plans verify target contents and native source-file revisions as well as timestamps. Image cache keys include metadata-change and creation times.
- Library search reuses an in-memory catalog snapshot and debounces typing instead of reparsing the catalog on every key press.

## 0.4.1

- Eigene minimalistische Toolbar-Symbole mit eindeutigen Plus-/Minus-Lupen, Tooltips und optionalen Beschriftungen.
- Mittige Nachbarvorschauen; Anzahl vorheriger und kommender Bilder getrennt unter Optionen → Einstellungen → Vorschauen einstellbar.
- Einstellungsdialog mit Anzeige, Oberfläche, Vorschauen, Navigation, Diashow und Leistung.
- GitHub-Vorbereitung, plattformübergreifende Build-Anleitungen und Windows-Release-Ablauf.
- SDK-Bootstrap repariert auch gleich große beschädigte Dateien; Paketprüfung und Installer-Regressionen laufen vor Windows-CI und Release-Build.

Erster vorgesehener Binärrelease: **Windows x64**. Linux und macOS werden zunächst als Quellcode mit Build-Anleitung angeboten. Native Tests auf diesen Systemen stehen noch aus.

## 0.4.0

- Optionale Nachbarvorschauen und Suche nach Bearbeitungsaktionen.
- Visuelle Stapelverarbeitung mit Exportvorschau, Dateiplan, Profilen und paralleler Verarbeitung.
- Bewertungen, Farbmarkierungen, Metadatenindex, identische und ähnliche Bilder.
- Internes Dateijournal mit Papierkorb und Rückgängig-Funktion.
- Bei Bedarf geladene RAW-Vorschau und Erweiterungsverwaltung; asynchroner Ordnerindex und begrenzte Caches.

## 0.3.x und früher

Grundlegende Bildanzeige und Bearbeitung, Auswahlzoom, Tastaturnavigation, Animationen/Mehrseitigkeit, Größenänderung, Feinrotation, Metadaten und verlustfreie JPEG-Transformationen. Der überprüfte Funktionsumfang und offene Punkte stehen in [PROJECT_STATUS.md](PROJECT_STATUS.md).
