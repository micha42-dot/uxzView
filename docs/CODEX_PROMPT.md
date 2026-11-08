# Codex handoff prompt — TurboJPEG hardening, first-open latency, freeze elimination

November 9 follow-up: [review corrections](REVIEW_FIXES_2026-11-09.md) supersede
the neighbour-admission and text-preview descriptions below. Neighbour loads
now pass an exception-safe, bounded native-header/cache gate. JPEG COM files use
Qt's text-aware path, and deletion resume carries ordinary navigation policy.
The older measurements remain historical; no new whole-viewer speedup is claimed.

Living brief for a follow-up agent. It records what these sessions changed, what was
*measured*, and what remains open. Update "Completed" and "Open" as work lands; do
not restate the invariants as new findings, and never replace a measured number
with an estimate.

## 1. Verified project facts

- Repository `IxView`, branch `main`, Windows 11, MinGW/ucrt64, Qt 6.11.2 from
  `.deps/sdk/ucrt64`. Prepend `.deps\sdk\ucrt64\bin` to `PATH` for every command.
- CMake + Ninja. Static libraries `ixcore` (codecs, storage, catalog, pipeline) and
  `ixui` (MainWindow, dialogs, ImageView), thin executable, lazily loaded plugins
  `ixarchive`, `ixrawpreview`, `ixpdf`, optional `ixextensions`.
- **The GUI target is `IxView` but the binary is `build/uxzView.exe`**
  (`CMakeLists.txt:39`, output name). `build/ixview_benchmarks.exe` carries the
  `--profile-*` harnesses; test binaries are `build/ixview_tests.exe` etc.
- Validation: `python scripts/validate.py` (~200 s) runs the Python tests plus ctest
  and writes `build/validation-summary.json`. Direct:
  `ctest --test-dir build --output-on-failure` (9 suites, 149-208 s depending on
  machine load) and `python -m unittest discover -s tests -p "test_*.py"` (47
  tests). `build/python-tests.log` always contains one deliberate
  "python-tests failed" line from
  `test_first_open_diagnostics.test_failed_process_preserves_partial_report_and_checkpoint`;
  the authoritative result is "Ran 47 tests ... OK" with exit code 0.
- State at `a304297`: 9/9 ctest, 47/47 Python, clean tree, build without warnings in
  the touched files.
- Measurement fixture: `build/fo-single/large.jpg` is a genuine 6000x4000 baseline
  JPEG (SOF0, 3 components, 23.2 MB, ~1 byte/pixel), so it decodes far slower than a
  typical 4:2:0 camera JPEG. Never describe it as "typical".

### 1.1 How the first-open numbers are produced

```
$env:PATH = "$PWD\.deps\sdk\ucrt64\bin;$env:PATH"
$env:IX_PROFILE_IMAGE_LOAD='1'; $env:QT_FORCE_STDERR_LOGGING='1'
$env:QT_MESSAGE_PATTERN='%{time process} %{message}'
Start-Process build\uxzView.exe -ArgumentList '--portable','build\fo-single\large.jpg','-platform','offscreen' `
  -RedirectStandardError build\app.err -PassThru
# read build\app.err: "ix-startup <stage>" and "ix-load <stage>" lines, ms offsets from
# process start. The image is visible when "ix-load install status" appears.
```

`--portable` keeps the profile out of the user's roaming directory. `-platform
offscreen` is mandatory here: agent sessions have no interactive desktop, so
`MainWindowHandle` stays 0 and `CloseMainWindow()` silently does nothing.

```
build\ixview_benchmarks.exe report.json --profile-first-open    <image> -platform offscreen
build\ixview_benchmarks.exe report.json --profile-prefetch      <image> -platform offscreen
build\ixview_benchmarks.exe report.json --profile-first-preview <image> -platform offscreen
python scripts\measure_first_open.py --image <image> --samples 3
```

**Rule:** any change on these paths needs an *interleaved A/B on one binary* (build
once with an env switch, alternate runs, then remove the switch). Numbers taken at
different machine-load states are not comparable - the same open of the same file
measured 422 ms and 674 ms in this repository within one hour. Use
`QT_MESSAGE_PATTERN` offsets rather than PowerShell stopwatches; they exclude shell
and process-creation noise.

## 2. Invariants — do not weaken while fixing anything

- Mapped image buffers: owner-only permissions, UUID file names, deletion moved to a
  single deleter thread, startup sweep for leftovers, unmap before delete.
- Preview/thumbnail cache: decode de-duplication with `shared_ptr` state, generation
  counters plus `QPointer` lifetime guards; cancellation is cooperative and never
  preempts a running native read.
- File publication: atomic no-replace (`RENAME_NOREPLACE`, `renameat2`,
  `renameatx_np`, `link()+unlink()` fallback), source revision compared before and
  after, `QSaveFile` staging with a re-check before `commit()`.
- Archive and extension hardening: path-traversal rejection, per-entry size caps,
  CRC validation, HTTPS-only downloads, SHA-256 verification, staged rename.
- Join semantics in `src/ui/worker_wait.h`: joins run a real event loop so paints,
  timers and input stay alive; reentrancy is blocked by `DocumentActivity` (`busy()`
  in every command dispatch) and by `closeEvent` ignoring a close while a transaction
  owns the document. Pure progress windows reappear when dismissed; cancellable ones
  stay hidden.
- Codec contract (`docs/ARCHITECTURE.md`): a codec owns the full document contract
  (pixels, path, format, page fields, `fileBytes`, metadata, `sourceHasExif`).
  Decoders must write through `QImage::bytesPerLine()`; library pitch `0` means
  tightly packed. `CodecRegistry::read` keeps the first accepting codec in charge,
  including its failure, and conflicts are resolved inside that codec.
- Every `IX_TEXT` string needs a row in `resources/languages.json` (five columns:
  de/en/ko/ja/fr, `%1` placeholders preserved); `test_localization.py` enforces it.
- **Read-join contract** (`ImageLoadCache::read`, `src/core/preview_service.cpp`):
  one `ActiveRead` per `{path, revision, page}` under `cacheMutex`. A second reader
  waits for the owner (20 ms polling, cancel-aware), shares the decoded
  `ImageDocument` (implicitly shared pixels, no copy) and decodes itself if the owner
  failed, so diagnostics stay with the caller. A joiner must still call
  `beforeDecode()` (the RAW/stage preview) *before* it waits and never while holding
  `cacheMutex`. Reentrancy must never wait on itself.
- **Cache epoch vs content generation**: `cacheEpoch` guards cache ownership and is
  bumped by `configure()`, `clear()` and `invalidate()`. `contentGeneration` is
  bumped only by `clear()`/`invalidate()`. Joining is gated on
  `contentGeneration`; gating it on `cacheEpoch` breaks the startup join (the window
  reconfigures the cache while it is being constructed), gating on nothing lets a
  cleared cache be repopulated from an older in-flight decode.
- **Removal / black state**: a document with a path but no pixels *is* the black
  state (`image_view.cpp:204` fills pure black, `main_window.cpp:1001` sets it),
  and `refreshState()` already disables every path command for it. The deletion
  path reuses it when `navigation/afterDelete` is 1 (the default);
  `removedDocumentPath_` plus `resumeNextPath_`/`resumePreviousPath_` let
  `navigate()` resume from the removed slot, captured before the directory changed
  so it survives an index rebuild. Do not "simplify" this back into
  `installDocument({})`: an empty path drops the listing and the arrow keys stop.
  Two coupling rules follow from the state: `reconcileFilePath()` must skip
  `removedDocumentPath_` (the batch and search flows would otherwise clear the path
  and mark the document modified, so a file the user only deleted would later ask
  about unsaved changes), and `installDocument()` clears the resume slot so a failed
  re-open of the same path cannot redirect navigation. The directory watcher needs
  no special case - it only calls `refreshFiles(true)` - and `dispatch()` already
  blocks delete/rename/move/copy while `document_.empty()`. "reopen" stays enabled
  on purpose: restoring the file from the trash and reopening it is a real flow.
  `isRemovalPlaceholder()` is the single predicate for the state; refresh, new
  window, the batch dialog and the pipeline dialog must not hand the removed path to
  anything that reads it (they skip it or pass no input).
- **Removal responsiveness**: the black state is set right after the confirmation,
  *before* the recycle-bin move, because `ImageView` draws an existing `image_` even
  when `unavailable_` is set (`image_view.cpp:203`) - so the view must be emptied, not
  just flagged. The failure path restores `view_->image()`, zoom and fit; the
  authoritative state change (`installDocument` of the removal document) still happens
  after the move. Do not "simplify" this by moving the install before the move: that
  would clear the document and its history before knowing whether the file is gone.
  A failed move restores image, zoom and fit but not the selection or scroll position -
  accepted, the path would need a failing recycle-bin call to show up. The test
  `unavailableViewNeedsAnEmptyImageToPaintBlack` pins the paint contract (an unavailable
  view still draws its image, so showing black requires an empty one).
- **Startup prewarm**: started in `main()` *after* `PluginStore::prepareStartup()`
  (a cache hit bypasses the codec registry, so a disabled format must never be
  decoded behind the policy) and *after* the language validation (that branch calls
  `showHelp()`, which exits without unwinding). No filesystem or `QSettings` I/O on
  the GUI thread for it - `prewarmImage()` only posts the task. `main()` holds
  `finishPrewarm()` in a `qScopeGuard`, and `finishPrewarm()` cancels, drops queued
  work and then joins.

## 3. Completed (committed and validated)

| Commit | Change |
| --- | --- |
| `cd9627d` | GUI joins keep paints/timers/input alive; saves cancel before commit |
| `317389c` | Save progress window has a working cancel button; joins stay input-capable |
| `8212149` | F3: status bar names the running transaction; rejected commands explain themselves |
| `9aee79b` | Four audit fixes: row stride, `fileBytes`, `accepts()` size limit, in-codec Qt retry |
| `1493abe` | Handoff document for the follow-up session |
| `1035acb` | Neighbour prefetch; header-first JPEG acceptance; two bench profiles |
| `1be6d4c` | Startup prewarm of the command-line image + in-flight read join |
| `5fd35bf` | Join hardening: stage preview, startup I/O, exit path |
| `a304297` | Prewarm starts after the language check (showHelp/exit ordering) |
| `e58036f` | Correct the AUD-7 precedent: swap permissions are POSIX-only |
| `c99f832` | Record the large-document boundary of the prewarm (PERF-5) |
| `9b868a1` | Deletion ends in the black state; new `navigation/afterDelete` |
| `2ac659d` | Close the black state's couplings (reconcile, resume slot, status label) |

### 3.1 TurboJPEG fast path correctness (`9aee79b`)

The fast path decodes into `Format_RGB32` with `TJPF_BGRX` on little endian and an
explicit stride, so widths that are not a multiple of four no longer shear rows
(previously silent corruption, including in batch exports). Peak memory drops from
~7 B/px to the 4 B/px the size guard already assumed. `finishDocument` takes the size
from the already-open file, `accepts()` mirrors the 256 MiB in-memory limit of
`read()`, and a decode-time refusal retries Qt's tolerant reader *inside* the owning
codec. `inspectJpegHeader()` is a real segment walker (ICC detection is no longer
limited to the first 64 KiB).

### 3.2 Neighbour prefetch (`1035acb`)

`MainWindow::schedulePrefetch()` runs after every successful install and after the
directory listing arrives. It decodes the next file in the last navigation direction
through the *same* `loadImage()` pipeline (so the speculative display matches the
user's colour/sharpen settings), on a dedicated single-thread `QThreadPool` at
`QThread::LowPriority`, and admits the result to the bounded `ImageLoadCache`.
`ImageLoadCache::hasDocument()` prevents duplicate work, `prefetchCancel_` supersedes
stale speculation, the worker captures no `MainWindow`, and speculation stops for
modified/animated documents, above 64 MiB of pixels, at the ends of the listing and
while a transaction holds the window.

Measured with `--profile-prefetch` (two generated 12 MP JPEGs, two independent runs):
cold install 215.2 ms / paint 227.1 ms versus prefetched install **1.36 ms** / paint
31.5 ms; the speculation itself finished 206 ms after the first image appeared
(second run: 257.7/268.5 -> 1.53/30.7). Regression test:
`prefetchesTheNeighbourInNavigationDirection`.

### 3.3 Header-first JPEG acceptance (`1035acb`)

`inspectJpegHeader()` records `hasExif`, `width` and `height` from the SOF, so
`accepts()` builds a `QImageReader` only when an Exif APP1 can carry an orientation
and `read()` skips that pass entirely for plain files (three header passes became
two). The acceptance read is 64 KiB and extends to 256 KiB only when the frame header
sits further out. Linked-probe measurement on the 23 MB fixture: **0.646 ms -> 0.341
ms** per `accepts()` over 40 interleaved runs, identical decisions. `accepts()` also
refuses a zero frame size (DNL) and leaves it to Qt.

XMP is not a rotation source for Qt - verified by construction, not by assumption: a
JPEG whose XMP APP1 carries `tiff:Orientation=6` and that has no EXIF decodes
6000x4000 both through the turbo path and through Qt's path (forced with an ICC
APP2), while the same file with a real EXIF APP1 decodes 4000x6000 on both. The
`hasExif` gate is therefore behaviour-preserving.

### 3.4 Rejected with measurement: staged JPEG preview

`TurboJpegCodec::readScaled()` (libjpeg-turbo `tj3SetScalingFactor`) was implemented,
tested and benchmarked, then removed: DCT scaling skips IDCT and upsampling but not
entropy decoding. DCT-factor sweep of the 6000x4000 fixture: 1/8 = 254.5 ms,
1/4 = 368.0 ms, 1/2 = 503.5 ms, 1/1 = 607.1 ms - a 1/4 decode (0.4 % of the pixels)
still costs 395-576 ms against 476 ms for the full decode, plus a second 23 MB read.
`--profile-first-preview` stays as the harness that would prove a future staged
approach (it reports `first_paint_ms` separately from `document_paint_ms`). Do not
retry scaled JPEG previews without new evidence.

### 3.5 Startup prewarm and the in-flight join (`1be6d4c`)

The first open used to be the sum of two serial halves: everything before the open
request (plugin discovery, language, theme, `MainWindow` construction, `show()`) and
then the decode. `main()` now prewarms the command-line file through the same
`loadImage()` pipeline while the window is still being built, and the open joins that
decode instead of repeating it.

Interleaved A/B on one binary, 6000x4000 fixture (`install` = last install stage):

| Pair | without prewarm | with prewarm |
| --- | --- | --- |
| 1 | 456 ms | **319 ms** |
| 2 | 422 ms | **324 ms** |
| 3 | 425 ms | **324 ms** |

98-137 ms (23-30 %) earlier, exactly one decode instead of two, install 4 ms after
the decode ends. A later single run with the final ordering: open request at 169 ms,
decode 377 ms, install 449 ms (without the prewarm 169+377 = 546 ms).

Prewarming earlier than plugin discovery was rejected: a cache hit bypasses the codec
registry, so it could serve a format the plugin policy later rejects. Prewarming
before the language check was rejected too - that branch calls `showHelp()`, which
exits the process without unwinding (`a304297`). Tests:
`prewarmsTheCommandLineImage`, `joinsAConcurrentDecodeOfTheSameFile`.

Implementation notes that matter: `ImageLoadCache::read()` keeps an `ActiveRead` per
`{path, revision, page}`; a waiting reader receives the decoded `ImageDocument`
(shared pixels, no copy) and decodes itself when the owner failed. The owner
publishes the document *before* its cancellation check, so a joiner still gets pixels
the owner no longer needs.

### 3.6 Join hardening (`5fd35bf`)

- A joined read publishes the stage preview (`beforeDecode()`) *before* it waits, so a
  RAW file keeps showing its embedded preview instead of an empty window for the
  whole develop. Test: `aJoinedReadStillPublishesTheStagePreview` (a paused codec
  proves exactly one decode and one preview).
- `prewarmImage()` does no filesystem or `QSettings` I/O on the GUI thread: a stalled
  network share must not delay a window that does not exist yet.
- `finishPrewarm()` cancels, drops queued work, then joins; `main()` holds it in a
  `qScopeGuard` so a throwing window constructor cannot leave a decode writing into
  destroyed caches. A decode already past its cancel boundary still finishes - the
  same as closing the window during an open, which joins its worker too.

### 3.7 Validation and documentation notes

`CHANGELOG.md` is written in English and stays user-visible (no internal identifiers);
`docs/ARCHITECTURE.md` describes the pipeline, not the history. `build/python-tests.log`
always contains one deliberate "python-tests failed" line (see 1.) - the authoritative
result is 47/47 OK with exit code 0. First paint after an install costs 272 ms once
and 6-25 ms for every later image (same bench): surface/backing-store warm-up plus the
heavy-fit raster built by the existing first-paint path, not a per-image cost. Do not
"optimise" it with a pre-scaled worker raster without re-measuring this split.

## 4. Open findings (source-verified, not yet fixed)

- **AUD-5 (P2)** - two hard blocking joins. `src/ui/dialogs.cpp:459-461` (histogram)
  and `:604-608` (batch/pipeline result) call `watcher.waitForFinished()` after
  `dialog.exec()` has already returned: no event processing and no progress
  indication while the last file finishes. Route both through
  `ix::waitForGuiWorker()`.
- **AUD-6 (P3)** - catalog serialisation. `src/core/image_catalog.cpp:22` holds a
  file-scope `QMutex` and `:187-190` waits up to 5 s for the cross-process `QLockFile`
  while holding it, around a JSON read of up to 256 MiB. Callers are workers
  (`library_dialog.cpp:233`, `:265`, `:351`), so a GUI stall is unconfirmed; the risk
  is multi-second waits under contention. Also drop the dead `image_catalog.h`
  include in `src/ui/main_window.cpp:16`.
- **AUD-7 (P3)** - OCR input permissions. `src/core/recognition_input.cpp:29-48` writes
  `input.ppm` and copied `*.traineddata` into `AppStorage::cacheRoot()/tool-jobs/run-XXXXXX`
  without tightening permissions. The swap directory does this **only on POSIX**
  (`image_storage.cpp:199-202` guards `ReadOwner|WriteOwner|ExeOwner` with
  `#ifndef Q_OS_WIN`, and on Windows `QFile::setPermissions` is close to a no-op), so
  decide the Windows story explicitly - an ACL on the job directory plus a startup
  sweep - instead of copying the swap code. A crash leaves pixels behind either way.
- **AUD-8 (P3)** - histogram churn. `src/ui/dialogs.cpp:441-456` allocates
  `image.copy(0, y, width, 1).convertToFormat(Format_ARGB32)` per row (thousands of
  allocations on a 25 MP image). Use `constScanLine` with one scratch row.
- **PERF-1 (P2)** - priority inversion in the read join. A foreground open that lands
  while a *prefetch* decode owns the `ActiveRead` waits behind a
  `QThread::LowPriority` thread (no boost like `decodePreviewPixels`, because a boost
  needs a raw `QThread*` that can dangle). Cheapest fix without dangling pointers:
  `MainWindow::openRequested()` sets `prefetchCancel_` when the new request targets
  the prefetched path, so the join is refused and the foreground decodes at normal
  priority. Needs a measurement of a LowPriority decode under CPU load before/after.
- **PERF-2 (P3)** - `prepareDisplay()` has no join of its own. An open that joins the
  read, then arrives before the prefetch stored its presentation, recomputes the
  sharpen/colour convolution (~50-100 ms at 24 MP with display sharpening on). Only
  active with `view/sharpenDisplay` or a non-sRGB profile.
- **PERF-3 (P3)** - first-install settings write. `installDocument()` calls
  `updateRecentSetting()` on the GUI thread; the first `QSettings` write costs 19.7 ms
  (measured on a warm profile, first install only) and sits between the install and
  the first paint. Deferring that write past the first paint would recover it without
  changing ordering guarantees; measure before and after.
- **PERF-4 (P4)** - budget ordering. The prewarm is admitted against the default
  192 MiB document budget because `MainWindow` applies the user's configuration later;
  a 24 MP document therefore enters the cache and may be trimmed immediately after.
  Harmless today (defaults), but keep it in mind if the defaults shrink.
- **PERF-5 (P4)** - documents above the cache admission limit. `ImageLoadCache::read`
  only stores originals up to 96 MiB of pixels, so a prewarm of a larger file cannot
  be reused once it has finished: if the window opens *after* the decode completed, the
  open decodes the same bytes again. The cost is CPU only, never latency - the open
  never waits for a finished prewarm. Measured with a 5200x5200 PNG (103.1 MiB of
  pixels, cheap to regenerate with System.Drawing, `build/huge.png`): the open arrived
  at 216 ms while the prewarm ran to 361 ms, so the join covered it (install 415 ms
  against 499 ms without the prewarm). Keep the admission limit; if CPU on very large
  files ever matters, decide between skipping the prewarm above the limit (needs the
  size before decoding) and admitting one prewarmed original - both need numbers.

## 5. Documented limitations - keep as contracts, do not "fix" by weakening

- Cancelling a save stops the commit, not the encoder: the running encode finishes and
  is discarded. The progress window closes immediately while the main window stays
  busy until the worker returns. Cancelling mid-encode would need a cancel token
  through `QImageWriter`/libjpeg/libwebp.
- Cancellation never preempts a running native read either. `loadImage()` and
  `ImageLoadCache::read()` check the token at pipeline boundaries only, so a decode
  that is already inside `tjDecompress2`/libtiff finishes. This is why
  `finishPrewarm()` can still wait for one frame.
- The read join has no timeout. A wedged owner (for example a blocking network read
  inside a decoder) blocks the waiting reader; a new open cancels the waiter's token
  within ~20 ms, so the window stays usable, but the wedged decode itself remains
  stuck on its worker thread and `~MainWindow`/`finishPrewarm()` will wait for it.
  Add a timeout only together with a measurement that a legitimate slow decode cannot
  hit it.
- Rejected commands are explained, not queued. Only `pendingNavigation_` and
  `deferredOpen_` queue work deliberately.
- Fullscreen shows no busy hint (the status bar is hidden there); only the wait cursor.
- Prefetch depth is one file in one direction. Deeper speculation is unmeasured and
  would compete for the bounded cache; do not raise it without numbers.
- The deletion resume needs a listing to capture the neighbours from. If a single
  file was opened and deleted before its directory index arrived, the first arrow key
  starts at the edge of the listing. Capturing the slot later would have to survive a
  second directory change; not worth the complexity unless someone reports it.
- Deleting the open file leaves that path in "recent files" (it was already there
  from opening it) and keeps "reopen" enabled, which reports a readable error if the
  file really is gone - deliberate, because restoring it from the trash and reopening
  is the same gesture. The status bar distinguishes the two black states
  ("Datei gelöscht" versus "Datei nicht lesbar"), and the preview strip handles the
  resulting index -1 (it already had a guard for that).
- A joined reader does not re-validate the source revision itself; the key contains
  the revision and the owner performs the check, so this is safe by construction -
  keep it that way instead of adding a third stat call.
- `isRemovalPlaceholder()` couples path and empty pixels. A regularly empty document
  whose path happens to equal `removedDocumentPath_` would count as the black
  state. Unlikely (it needs the exact just-removed path), but the robust fix is an
  explicit flag cleared by `installDocument`, touching navigate/reconcile/refresh -
  only worth it the next time that area changes.

## 6. Next steps, in order

1. **PERF-1 (P2)**: in `MainWindow::openRequested()`, set `prefetchCancel_` when the
   request targets the prefetched path (or whenever a prefetch is in flight), so a
   foreground open never joins a `LowPriority` decode. Add a test that an open of the
   prefetch target decodes on the foreground thread, and measure a LowPriority decode
   under CPU load before/after. Do not add a raw `QThread*` boost.
2. **AUD-5 (P2)**: replace the two hard `waitForFinished()` joins in
   `src/ui/dialogs.cpp` with `ix::waitForGuiWorker()` and keep the progress windows
   cancellable, then re-check the workflow suite (it drives both dialogs).
3. **PERF-3 + AUD-8 (P3, cheap and local)**: defer the first `QSettings` history write
   past the first paint, and give the histogram one scratch row. Both reduce
   measured GUI-thread time; re-run `--profile-first-open` afterwards and update 3.7.
4. **AUD-6 (P3)** catalog lock ordering, then **AUD-7 (P3)** OCR input permissions with
   a startup sweep. Neither is measured yet - instrument before and after.
5. **Re-measure the first open end to end on an idle machine** with the interleaved
   A/B recipe from 1.1 and record the result here. The current 319-324 ms vs 422-456 ms
   pair was taken on a loaded agent machine; a quieter baseline is worth having.
6. Only with new evidence: "do not decode the full resolution into the document"
   (touches export/edit semantics and the storage contract) and prefetch depth 2.

## 7. Working rules for this repository

- Measure before claiming, and measure *interleaved A/B on one binary*. A single
  before/after pair from different machine-load states is not evidence; two numbers
  with a factor of 1.6 between them were measured an hour apart in this repository.
- Keep scratch probes and fixtures out of the source tree: probes in `%TEMP%\ixperf`,
  generated images in `build/`. Nothing in `build/` is committed.
- `ctest --test-dir build --output-on-failure` plus the Python suite before every
  commit; a commit message states the measured numbers *and* what was rejected.
- Never weaken cancellation, cache budgets, join ordering or the plugin policy to make
  a test pass. When an existing test contradicts a new mechanism, decide which
  contract is right and encode it (`cacheClearDoesNotRestoreOlderInFlightDocument`
  forced the `contentGeneration` split; the paused-codec test needed a real file
  because `CodecRegistry::read` validates the path).
- Every `IX_TEXT` string needs its row in `resources/languages.json`
  (`test_localization.py`).
- Update `CHANGELOG.md` (English, user-visible wording) and this document in the same
  commit as the behaviour change, so the numbers here never drift from the code.





