# Änderungen

## Unreleased

- Keep the window responsive while a GUI-owned worker finishes: the join loop no
  longer suppresses user input (commands stay blocked by DocumentActivity,
  closing stays deferred, and only the caller's modal window takes clicks).
  Saving is cancellable through the progress window's cancel button, which
  discards the staged output before commit and keeps the document's edits, and
  joins slower than two seconds are logged. Progress windows that cannot be
  cancelled reappear when dismissed, so a blocked window never looks idle.
- Name the running transaction in the status bar for as long as it holds the
  document ("Bild wird geladen …", "… gespeichert …", "Dateioperation läuft …"),
  and answer a command that is refused during it with "Bitte warten – …" instead
  of ignoring the input silently. The hint disappears with the transaction.
- Remove large backing files released on the GUI thread through a single
  deleter thread instead of deleting them inline, so replacing a large image or
  clearing undo history no longer stalls the window. Add fallback regression
  tests for grayscale, EXIF-rotated and ICC-tagged JPEGs.
- Decode unrotated, non-profile-embedded JPEGs via libjpeg-turbo directly,
  saving the Qt JPEG handler overhead. Large images and forced spills remain
  on Qt's direct-mapped-buffer codec for file-backed pixel storage. The fast
  path writes through the buffer's own stride into the document format, so
  JPEGs whose width is not a multiple of four decode identically to Qt, and it
  mirrors the Qt document contract (`fileBytes` included). `accepts()` now
  applies the same size limit as `read()`, and a decode-time refusal inside the
  turbo path retries with Qt's tolerant reader instead of failing the file.
- Prefetch the neighbour in the navigation direction on its own low-priority
  thread and admit it to the bounded image cache, so stepping through a folder
  reuses a decoded original instead of decoding again (a 12 MP JPEG installs in
  1.4 ms instead of 215 ms; new `--profile-prefetch` benchmark). Speculation stops
  for modified or animated documents, above 64 MiB of pixels, at the ends of the
  listing and while a transaction holds the window.
- Accept JPEGs from their segment header first: the frame size comes from the SOF
  marker and `QImageReader` is only consulted when an Exif APP1 can carry an
  orientation, so a plain JPEG needs one header pass instead of three. The
  acceptance read shrinks to 64 KiB and extends only when the header needs it;
  measured 0.65 ms to 0.34 ms per open on a 23 MB file with identical results.
- Add `--profile-first-preview` (first paint vs. document paint) and
  `--profile-prefetch` (cold vs. prefetched neighbour) to the benchmark harness.

- Build the reusable display raster during the first paint of a heavy fit, so the
  follow-up repaint after a new fit or zoom reuses it instead of repeating the
  full smooth downscale. Keep the deferred raster for ordinary sizes. Log GUI
  worker joins that take longer than two seconds to make intermittent stalls
  observable.
- Ask only once before replacing an existing file: the save chooser no longer
  confirms overwrites itself, so Save As onto the current or another existing
  file shows a single app question with the changed-on-disk wording. Add a
  regression that counts the overwrite questions for Save As over the current file.
- Keep Windows Paste availability checks off the external clipboard data path:
  query advertised native formats without retrieving payloads or waiting for
  another process's clipboard lock. Retain actual Qt paste behavior and add
  image/URL/text and locked-clipboard regressions. Extend optional image-load
  diagnostics through application startup and window construction.
- Sharpen RGB32/ARGB32 directly with an exact byte kernel, using baseline SSE2
  for groups of four interior pixels and a portable scalar path elsewhere.
  Remove the two RGBA64 working buffers and format round trips. Preserve alpha,
  padding/source data, metadata, legacy DPR/offset behavior and conservative
  admission checks. Add exhaustive byte/sum and scalar/SIMD validation.
- Skip obsolete queued full-image opens and unnecessary presentation work after
  a newer explicit request. Preserve current image/history/selection on cancelled
  completion, keep unreadable errors distinct, and deliver deferred requests at
  worker completion without an extra polling delay. Retain dirty-document,
  RAW-preview, page/animation, save and relative-navigation ordering safeguards.
- Share overlapping eligible PNG/BMP pixel decodes between foreground opening
  and thumbnails. Keep per-reader metadata/revision checks, cancellable preview
  waits, independent generations and reader-specific error retries. Restore
  preview worker priority after foreground joins. Add race/failure regressions
  and concurrent decode CPU/private-memory profiling.
- Reuse full pixels already decoded for eligible PNG/BMP neighbor thumbnails
  when opening that file. Keep foreground metadata and revision checks; share
  existing cache limits and prioritize originals and nearest neighbors.
  Retain efficient reduced JPEG decoding and all animation/RAW/PDF paths.
- Reuse prepared color-managed/sharpened display pixels when revisiting cached
  originals. Keep ordinary sRGB/unmanaged display on its direct path. Share the
  existing image-cache allowance, reclaim presentation buffers before originals
  and bound combined image/preview ownership by the image-memory budget.
  Account for retained cache cost when admitting new batch work. Add native
  window navigation profiling and low-budget/race/policy regressions.
- Coordinate estimated pipeline memory across simultaneous batch runs before
  decoding; keep small jobs parallel and serialize unknown/oversized work.
  Cancel queued jobs without decoding or publishing, release reservations on
  every exit and recheck sources after waiting. Retain existing disk-backed,
  image-quality, revision and atomic-save safeguards. Add a real 16-bit PNG
  batch memory/time profiler and admission/cancellation/rewrite regressions.
- Stabilize the Qt 6.8 file-open regression driver with focused filename input,
  Unicode/space paths and bounded dialog diagnostics. Retain the 300-second
  suite limits; print last-entered test checkpoints before full CI logs.
- Query macOS backing space on the opened file's filesystem, avoiding Qt's
  volume-URL resolution for APFS firmlinks. Retain the free-space reserve,
  real preallocation, private permissions and failure cleanup.
- Ask before discarding edits when no preference is stored; retain explicit
  choices. Use one discard decision for document replacement and closing;
  cancellation, unidentified answers and failed saves never authorize discard.
- Join non-cancellable save/file workers through the future event loop, with
  progress shown only after 200 ms. Avoid short-operation progress flashes and
  completion-during-show races; retain GUI timers, transaction gates and input
  suppression. Add a reproducible real-PNG save/progress benchmark.
- Use Michael Förtsch's transparent logo variants for light/dark app themes,
  including live system-theme changes and the About dialog. Regenerate neutral
  transparent launcher icons for Windows, Linux and macOS from the grey variant.
- Accelerate 8-bit gamma resize output with inverse byte boundaries and an exact
  fallback near rounding ties; retain continuous 16-bit output. Initialize the
  small read-only lookup lazily and include it in the memory estimate.
- Parallelize large RAM-backed nearest-neighbor outputs using the shared resize worker
  allowance and adjacent row blocks, without extra pixel buffers. Share worker
  joining/exception handling across filter paths; cancel before output allocation
  for pre-cancelled nearest requests. Expand native-format and gamma-boundary
  reference coverage, and support focused reproducible resize benchmarks.
  Keep mapped nearest output serial after large probes showed higher page
  pressure and slower bulk writes with several writers.
- Keep wide resizes parallel with bounded column/row tiles, including 50,000-
  and 100,000-pixel axes. Account for tile-local work in the resize budget.
- Accelerate nearest-neighbor copies with fixed-size pixel copies and reuse of
  identical output rows. Use baseline SSE2 for exact 8-bit output quantization
  on x86, retaining scalar tie/overflow handling and the other-architecture path.
- Reuse an exact, lazy 16-bit input transfer table for gamma resampling; account
  for its 512 KiB allocation. Extend pixel comparisons to all filters in parallel,
  wide/mapped buffers, byte ties and every 16-bit input value.
- Make Settings controls easier to operate: larger click targets and popup rows,
  readable labels that wrap with the available width, and a dedicated Previews
  page. Keep dropdown arrows small, disable dependent labels with their fields,
  and require focus before the mouse wheel changes numeric or dropdown values.
- Accelerate large resizes with reusable normalized filter rows, exact byte
  quantization and at most four participating threads per operation. Bound extra
  workers across simultaneous resizes, and retain compact rows for kernels that
  would overflow the normalized-row cache even with column tiling.
- Keep legacy 8-/16-bit pixels, including colors under zero alpha, while counting
  per-worker row buffers in the live RAM estimate. Separate resampling from
  general pixel processing; add a reproducible 1920×1024 → 10000×5333 benchmark.

## 0.5.4

- Add optional disk-backed original-pixel buffers with reserved storage, an
  aggregate budget, last-reference cleanup and locked crash cleanup. Default to
  64 GiB backing; retain pixel Undo and discard-by-default behavior.
- Extend bounded row resampling to 16-bit output, include available backing in
  resize validation, and decode PNG/sequential JPEG directly into mapped pixels.
  Keep large progressive JPEG and unverified codecs explicitly RAM-limited.
- Stream large PNG/JPEG exports atomically, flatten alpha per row, preserve
  source geometry in display and report format/metadata limits explicitly.

- Select image-buffer RAM limits at startup from physical RAM (automatic half-RAM,
  512 MiB to 8 GiB) or performance/memoryLimitMiB. Bound 32-bit builds, align Qt's
  decoder limit and expose --memory-info. Changes take effect after restart.
- Resample 8-bit images with bounded row conversion/filtering and native output,
  preserving legacy rounding. Show and validate the resize operation's live maximum
  and increase the budget-checked geometry ceiling to one million pixels per axis.

- Process native RGB32/ARGB32 grayscale in one output buffer with exact legacy
  rounding, including floating-point ties. Preserve every 8-bit RGB color and
  image properties while halving its image-buffer working set.
- Build Windows Exiv2 for local metadata input without unused HTTP/Curl readers.
  Keep Qt TLS libraries explicit, use the native network-information backend and
  omit its redundant GIO dependency chain without dropping image formats.

- Read multi-page PDFs through a lazy Qt PDF/PDFium module, with page navigation,
  white page backgrounds, bounded thumbnails and image export. Add PDF file
  associations; keep PDF writing and password-protected documents unsupported.
- Add Navigation settings for unreadable folder entries: skip (default), show a
  full-path error or continue on a black display. Keep navigation active on black
  entries, bound skip retries and retain edits after failed explicit opens.

- Process negative, channel-display and channel-swap edits directly in native
  RGB32/ARGB32 output buffers. Retain exact pixels, alpha and image properties;
  halve their source/working/output image-buffer estimate from 16 to 8 bytes
  per pixel while respecting the configured RAM/storage budgets. Other formats
  retain their guarded processing paths.
- Apply the selected appearance to native Windows title bars, including older
  Windows 10's DWM fallback, late-created dialogs and recreated window handles.
  Keep frame updates independent of image loads and window geometry.
- Include CMake-derived Windows product/version information with matching Unicode
  translation metadata. Validate fixed/string versions through the Windows API
  during release preparation without executing the inspected binary.
- Store fitted-view and manual-zoom smoothing independently. Migrate the old
  shared preference to both choices and switch rendering policy without settings
  IO on each zoom or redundant repaint requests.
- Resolve post-delete navigation against the source path when its snapshot index
  is stale; skip duplicate source entries. Queued manual navigation takes priority
  over hotfolder auto-opening when a new directory snapshot arrives.
- Guard late OS file-open callbacks with the window lifetime; consume completed
  preview/save results and keep GUI paints alive during required file-worker joins.
  Report failed folder watches instead of showing hotfolder mode as active.
- Reject EXIF resolution values outside the finite unsigned-rational range
  before conversion; adapt the denominator for valid high-DPI values rather
  than overflowing a fixed milli-DPI numerator.
- Separate catalog warnings from file-operation failures. Successful direct and
  batch moves retain success status while displaying any metadata warning.
- Reuse one settings reader when capturing color-management and RAW-preview
  preferences for an image-load request.
- Load catalog snapshots and perform annotation IO off the GUI thread; retain
  owned library inputs and process GUI paints/timers while joining worker work.
  Filter existing extension rows during search, move folder enumeration to a
  worker, and remove the unused eager thumbnail dialog. Keep extraction/file
  phases and mandatory shutdown joins intact.
- Document concurrent worker progress callbacks and verify queued GUI delivery;
  retain the pipeline dialog's guarded, coalesced progress mailbox.
- Publish POSIX moves without replacing an occupied destination, including
  cross-volume staging; avoid Qt 6.8's unsafe raw-rename fallback on unsupported
  filesystems. Fail safely if no no-replace primitive is available.
- Apply classic batch rename annotations in one ordered, atomic catalog update,
  including completed moves on cancellation. Serialize process-local file work
  through that flush; retain latest unrelated annotations and revision snapshots.
- Bound pixel/filter image working sets to the configured RAM budget before allocation, release
  convolution/histogram inputs before output conversion, and reject invalid
  filter geometry without overflowing kernel-size arithmetic.
- Scan native 32-/64-bit rows for automatic border cropping without a converted
  full-image buffer. Preserve Qt pixel semantics for other formats.
- Recreate missing root menus before adding features and ignore a missing crop
  submenu instead of dereferencing null menu pointers.
- Reject invalid channel indices before swapping pixels instead of indexing
  outside the RGB channel array.
## 0.5.0

- Filter ordinary image extensions through a per-scan suffix set instead of
  testing every filename against the entire wildcard list. Preserve Qt matching
  for nonliteral filters and Unicode suffixes; retain sorting and cancellation.
- Remove obsolete batch labels promising replacement backups after file history
  removal; use plain Replace captions in all five languages. Clarify the cache
  admission threshold and add a reproducible 100 MiB load/crop/Undo benchmark.
- Build image-opening filters from reader extension metadata without discovering
  every decoder/encoder. Prepare concise filter labels and fallback geometry
  before showing the chooser; retain native dialogs where available.
- Allow selections to start on the image background and clip them to image
  edges, including aspect ratios. Keep reverse-drag endpoints pixel-inclusive;
  background-only drags create no selection.
- Own open-request paths across modal save questions so caller/directory changes
  cannot redirect an in-progress request. Preserve edits and Undo when navigation
  selects the current file/page; check page bounds before asking to save and
  revalidate after saving. Avoid needless reloads for these navigation commands.
- Discard unsaved edits on closing by default, including Escape and Exit.
  The existing optional save question now covers both image changes and closing;
  update its caption in all five languages.
- Keep the image canvas frameless in window and fullscreen modes, including
  fullscreen return. Retain the separator above image information.
- Compact Settings into nine stable categories, place previews in a frameless Display section,
  size small fields to content and wrap long translated checkbox captions.
  Reduce colour/rotation dialog defaults; shorten six menu captions in five
  languages. Start classic batch renaming in rename mode with relevant controls.
- Transfer decoded batch documents into processing so resized originals can be
  freed before output publication. Release transformed preview pixels before
  decoding the saved preview. Use hashed affected-path lookup for cache eviction.
- Keep the active file and ratings attached to the correct original through
  classic batch renames that reuse earlier source names. Relocate annotations
  once; skip rewriting the catalog for files without an entry.
- Preserve active image pixels and pixel Undo/Redo across batch renames when
  source/target revisions match; reject pixel reuse after intervening rewrites.
- Hold the file-transaction phase during classic batch dialogs, defer external
  opens and preserve unrelated image caches on completion or cancellation.
- Check source revisions before sending a file to native trash, including
  changes while its confirmation dialog is open.
- Limit the crop-aspect submenu to built-in/custom ratios and custom settings.
  Normal Crop still requires a selection. Clear a frame's ratio constraint when
  clicking to deselect it; preserve the chosen ratio through display refreshes.

- Remove the file journal, its dialog/menu actions and file-operation Undo.
  Preserve image-edit Undo and use the operating system trash for Delete.
- Write batch exports directly through the atomic Save path with revision,
  content, cancellation and source guards. Remove persistent replacement backups
  and redundant staged-file hashing/installation. CSV reports remain optional.
- Direct rename no longer hashes the full input or writes operation history.


- Retain unaffected decoded images, previews and APNG sessions after direct
  file operations. Invalidate only the source/target paths, preserving LRU order
  and preventing older in-flight reads from repopulating evicted entries.
- Verify copy/move source revisions around preparation and use source/target
  snapshots when checking whether displayed pixels still match the file.

- Keep image information visible while hovering commands; retain control tooltips.
- Send deleted images to the operating system trash, with no permanent-delete
  fallback. Restore through the OS.
- Make the histogram responsive and theme/DPI aware with selectable channels.
  Group extension actions for smaller windows, improve disabled-text contrast,
  keyboard-copyable literal information values, accessible names and label buddies.
  Add a dedicated UI quality suite and shared spacing constants.


- Release consumed asynchronous image/batch results instead of retaining them
  until the next job. Join a window's queued image work before destroying it.
- Move pixel Undo/Redo ownership and transitions into DocumentHistory. Apply a
  combined 128 MiB estimate including image properties/native metadata, count
  shared pixels/metadata once, and retain one oversized inverse operation.
- Protect lossless JPEG source/destination revisions before processing and commit;
  require absent destinations for page extraction saves. Bound implicit archive
  directories before KArchive constructs its tree. Add isolated maintenance tests.
- Read ZIP/CBZ image collections through an optional KArchive image plugin.
  Reuse page navigation, natural sorting, thumbnails and single-page export;
  read one image at a time without extracting files. Bound directory/entry
  sizes and reject unsupported or malformed ZIP variants. The Windows package
  reuses its existing KArchive runtime; PDF, CBR and JPEG XR remain unsupported.
- Use consistent menu checkmarks for exclusive choices and independent toggles.
- Mark window display-fit choices as an exclusive checkmarked selection. Remember the
  selected window/screen variant, reflect manual zoom and fullscreen return policy,
  and restore the actual selection when an action is blocked during loading.

- Apply window-fit changes made during fullscreen when returning to windowed mode.
  Prevent stale windows from overwriting a newer fit preference on close, and apply
  an accepted fit preference to a window whose local policy has fallen behind.
- Keep automatic window fitting active across image changes even when zoom locking
  is enabled. Recompute the scale for each image; preserve locked manual zooms.
  Cover wide-image navigation, preview/layout changes and startup at four DPI scales.

- Coalesce batch progress into a monotonic per-run mailbox with bounded GUI events;
  reject stale plan/progress deliveries after completion or a new run.

- Update Checkout, Setup Python, Upload Artifact and Cache to verified Node.js 24
  action releases across desktop, Windows release and Windows 10 workflows.

- Fix Linux deployment of dependencies used only by optional modules: scan build
  artifacts with their SDK RUNPATH before relying on installed relative paths.
  Add an actual ELF dependency/relocation regression to the Linux CI job.

- Give batch processing separate Processing/Results tabs without an outer scroll
  area; retain equal-width profile/destination buttons and their vertical gap.
- Present batch results through a lazy read-only table model, remove repeated
  full input-list copies during validation, and deduplicate added files with a set.
- Stream optional CSV logs in bounded chunks on the batch worker rather than
  assembling/writing the whole report on the GUI thread.
- Skip resource decoding and worker creation when no batch entry is eligible;
  cap worker count to eligible entries and contain non-standard decoder exceptions.
- Separate batch step editing and result presentation into dedicated UI modules.

- Resize proportionally while typing and use a single working standard-size
  dropdown with built-in and user dimensions.
- Start batch processing directly with an initially empty chain. Remove the
  preview/review prerequisite and before/after panels; retain automatic planning,
  freshness checks, cancellation and atomic output replacement.
- Add batch filename search, format filters (also in the file picker), balanced
  result columns, actual success/failure and size totals, and optional UTF-8 CSV logs.
- Add fullscreen scaling modes and a target-monitor selector, retaining the
  existing cursor/background/information options and legacy scaling preferences.
- Restore the subtle divider above file information and cancel manual zoom/pan
  when returning from fullscreen to the window's selected browsing mode.
- Move command-search visibility directly below the title-bar option in View.

- Apply German, English, Korean, Japanese and French immediately after accepting
  Settings, without restarting or rebuilding the window. Retranslate menus,
  toolbar/accessibility text and command search in place; refresh Qt translations,
  fonts and widget number locales while retaining edits, selection, zoom and Undo.
  Publish immutable translation catalogs for concurrent background readers and
  keep the process-wide locale fixed after startup.

- Separate read-only batch file planning from transformation/execution. Make
  planning and its SHA-256 reads cancellable; hash only eligible replacements,
  reuse watermark hashes and reject non-file destinations before decoding.
- Reject stale batch plans after changes or cancellation, contain worker failures
  and validate the current inputs and destinations automatically when starting.

- Add persistent System/Light/Dark appearance under Settings → Interface,
  applied on confirmation without restarting. Match menu and toolbar colors,
  refresh status/preview colors, and retain image pixels, selection and zoom.
  Avoid an unnecessary image display job or folder rescan for interface-only settings.
- Standardize dialog margins and spacing. Replace fixed coordinates in resize,
  color and rotation dialogs with layouts and scrollable content; keep confirmation
  buttons outside the scrolling area. Retain existing processing options and shortcuts.

- Refine toolbar vectors with 1.25-unit outlines, preserving recognizable
  symbols and click targets. Unify the light/dark zoom field and its visible
  dropdown arrow, soften separators and remove recessed status-label frames.

- Normalize temporary-path expectations in the module probe regression for
  macOS `/var` aliases and Windows short names. Exercise a noncanonical path
  on every host.
- Check installed Linux packages using their shipped XCB backend under Xvfb;
  add eight offline shell cases for backend/display selection and diagnostics.

- Speed up exact standard sharpening with integer arithmetic while preserving
  reference pixels, edge handling, alpha and image properties. The shared path
  also serves display/resize/default batch sharpening; measured 2.8–8.3× faster
  for the tested 6/24 MP images, with no new dependency or image buffer.

- Require metadata, RAW previews and the extension manager in desktop CI and
  Windows release builds; deliberately reduced developer builds remain supported.
- Use installed module paths in native Qt deployment and remove a warning in
  metadata-disabled builds. See the 27 October desktop build review.
- Verify installed module dependencies/exports, retain native install/package
  diagnostics and keep CTest suites serial with a bounded runtime.
- Add `--probe-modules` for profile-independent module checks; macOS CI uses the
  bundle executable's loader context for executable-relative dependencies.

- Keep saved-document and Undo source metadata flags aligned with the metadata actually written; valid empty metadata objects no longer invent EXIF/IPTC/XMP-loss warnings.

- Keep pre-clear decode completions from repopulating document/thumbnail caches or overwriting newer entries.
- Charge retained comments, document text, ICC bytes and native metadata/data areas to cache budgets, with bounded arithmetic and preserved payloads. Keep normal small-entry caching and the 96 MiB pixel admission ceiling.

- Skip the additional 256 KiB metadata-signature probe when Exiv2 already returned metadata information. Keep the conservative fallback and export-loss guard for unsupported formats or metadata-disabled builds. Add measured process-I/O comparisons and four export-policy regressions.

- Make preview invalidation fixtures require distinct encoded sizes/revisions and flush folder fixtures before assigning modification times. Keep the separate same-size/preserved-mtime regression.
- Use the bundled Cocoa platform for the installed macOS smoke test and retain version/image-probe startup logs with failure status.

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
