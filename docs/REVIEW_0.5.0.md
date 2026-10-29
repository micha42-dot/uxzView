# üxzView 0.5.0

The 0.5.0 development release combines the earlier 0.4.8 follow-ups with removal
of the file journal. This is a minor release because visible behavior and
features changed substantially; it is not a declaration of 1.0 readiness.

## Removed history, retained save safety

The file journal, history/recovery dialog and file-operation Undo actions are
removed from the viewer and library. Their backend sources and recovery tests
are removed as well. Direct file operations create no persistent history or
replacement-backup store. Image-edit Undo/Redo stays intact. Delete uses the
operating system trash, with no app fallback to permanent deletion.

Batch exports now share CodecRegistry's atomic QSaveFile transaction. Encoder
and metadata failures preserve the old target. A before-commit guard checks
cancellation and source changes after encoding; reviewed target revisions and
content hashes still protect replacement. Directory/symlink targets are rejected.
Successful replacements have no later disk-Undo. Optional CSV conversion reports
remain available because those describe a batch run rather than maintain file
history.

Classic batch rename returns successful path mappings to its owner in memory.
It retains applied moves even when updating the ratings catalog reports an
error, so the active image can follow its renamed file. Profiles, editing Undo
and existing tool installations keep their storage identities. New portable
imports exclude retired history data; existing backup originals are not
silently deleted or modified.

## Performance scope

Direct rename no longer reads the full source to compute a journal digest,
migrates history or writes per-operation JSON records. Copy also loses the
additional source-hash pass. Rename-only batch workers construct no decoder registry.
Batch conversion no longer encodes into a second named stage, hashes that
stage and installs it through history/backup moves; it encodes into the atomic
save transaction directly. Unchanged neighbor caches remain retained as in the
earlier file-operation follow-up.

The reproducible component measurement runs
`ixview_file_operation_tests --profile-rename report.json`: five SHA-256 reads
and five direct renames of the same 64 MiB temporary file, warm filesystem and
empty metadata catalog. The hash samples represent a removed component, not
measured total application time from the old version. No whole-app/cold-start
percentage speedup is inferred. Local evidence: build/0.5.0-rename-benchmark.json.

On the local Windows run, the removed hash component took a median 435.5301 ms
(428.69–557.21 ms); current direct rename took 0.8844 ms (0.82–1.26 ms).
Both series have five samples. This isolates an avoidable content read; real
images, populated metadata catalogs, other volumes and cold caches can differ.

## Verification and remaining limits

### Follow-up after removing the journal

The second audit reproduced four failures against the initial 0.5.0 code:

- Renaming `2.png → 1.png`, then `3.png → 2.png` made the active second file
  incorrectly follow the first file to `1.png`. The old path-to-path summary
  followed chains without accounting for a path reused by another original.
- Reconciliation relocated catalog annotations a second time. With the same
  reused names, that moved the second image's rating onto the first image.
- Native trash did not recheck the source after preparation. An injected rewrite
  reached the trash callback despite being newer than the prepared source.
- Renaming a file without a catalog entry still rewrote the whole catalog.
  An unchanged catalog fixture was reserialized, replacing its file revision.

Evidence before corrections: `build/0.5.0-followup-file-repro.txt` and
`build/0.5.0-followup-rename-repro.txt`. These are isolated temporary fixtures;
the trash regression uses a fake native-trash callback.

`BatchFileChanges` now tracks original identity and current path in two hash
maps, rather than resolving a path chain. It summarizes successful changes in
memory across runs of one dialog, including applied renames with catalog warnings.
The backend alone relocates annotations. An absent source annotation returns
without a catalog write. Batch completion invalidates affected source/output
paths; cancellation without changes retains cached neighbors. The dialog holds
the file-transaction phase and external opens keep their deferred priority.

The summary also carries the first source and last target revisions. When both
match the loaded document and current final file, reconciliation updates paths
and source metadata without decoding again or clearing pixel Undo/Redo. A rewrite
between moves invalidates that proof and uses the existing reload/dirty-image
protection instead. Native workflow cases include clean, edited and undone images
whose old names are reused by another file.

Trash uses the same preparation/revision checks as copy/move. The main window
also carries the revision captured before confirmation into the worker. This
detects observed changes; it is not an atomic filesystem lock against other writers.

The supported Qt baseline documents native trash on Windows/macOS and the
FreeDesktop trash implementation on Unix; missing trash support returns failure
([QFile 6.8](https://doc.qt.io/qt-6.8/qfile.html#moveToTrash)). Source inspection
of the [Qt 6.8.3 Windows path](https://github.com/qt/qtbase/blob/v6.8.3/src/corelib/io/qfilesystemengine_win.cpp#L1658)
and [the shipped Qt 6.11.2 path](https://github.com/qt/qtbase/blob/v6.11.2/src/corelib/io/qfilesystemengine_win.cpp#L1687)
confirms the native IFileOperation call with recycle flags. This inspection does
not replace real tests of network volumes, disabled trash or every desktop setup.
Both versions' [pre-delete callback](https://github.com/qt/qtbase/blob/v6.11.2/src/corelib/io/qfilesystemengine_win.cpp#L764)
also rejects an operation reported as permanent deletion rather than recycling.

The crop-aspect submenu contains ratios and custom-ratio settings only.
Normal Crop remains selection-dependent. Clicking to clear a selection releases
the old frame's ratio constraint, so removing the free-selection menu entry does
not trap subsequent selection in a stale ratio, including when the image fills
the view. Display-only refreshes retain both frame and ratio; the regression
toggles display sharpening twice before applying/undoing an actual crop.

The first full follow-up run passed all eight core suites but stopped in the
native 125% wheel fixture: its cumulative spy contained two signals where it
expected one. Three isolated repeats of the unchanged fixture passed. There
was no pre-injection event trace, so the cause of that one extra signal is not
proven. The assertion now measures the exact signal delta of the synchronously
injected wheel after geometry waits, and still rejects duplicate delivery of
that event. The product's wheel/navigation implementation is unchanged.
Initial evidence remains in `build/0.5.0-wheel-initial-failure*` and the three
`0.5.0-wheel-native-recheck-*.txt` reports. Fresh full validation passes all
33 steps, including the final ratio-preservation regression.

The replacement file-operation suite checks direct operations without history,
existing-target/source-change rejection, unavailable native trash, successful
rename with a catalog warning and rejected/cancelled atomic writes. Existing
batch-order/preview/format guards and active-image reload tests remain; tests
specific to the removed recovery/Undo subsystem no longer apply. The change
therefore reduces total test count without removing those save/pixel checks.

Evidence: build/0.5.0-followup-build.log, build/0.5.0-followup-validation-summary.json and
build/0.5.0-followup-portable.log. The build and SDK-independent portable runtime each
pass 417 Qt cases without failures or skips; 45 Python tests and all 33 validator
steps pass, including native Windows widgets at four DPI scales. QR recognition,
all five OCR models and relocated-profile startup pass without SDK paths.
The build has no compiler warnings and deployment resolves all 136 PE modules.
Counts and packaging checks are also recorded in [QUALITY.md](QUALITY.md).
Native Windows 10, Linux and macOS desktop verification remains
outstanding. Atomic output replacement is not a filesystem-wide lock against
uncooperative writers or a power-loss recovery guarantee. The visual pipeline
still clears caches globally. The JSON catalog still validates/reads a complete
snapshot per relocation, even when the no-write path applies; large batches need
profiling and coalesced catalog updates. Ratings remain path-based rather than
being an identity database for files replaced independently outside the viewer.
Main-window orchestration and combined process-memory budgeting remain future
refactor work. These limits are separate from the reproduced rename failures.
The native fullscreen harness reported unavailable desktop pixel readback in this
session. Structural geometry/cover/fence checks run, but cover colour is not
verified by that readback and a visually flicker-free transition is not established.
