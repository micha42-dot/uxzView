# Codex handoff prompt — TurboJPEG hardening and freeze elimination

Living brief for a follow-up agent. It records what this session changed, what it
verified, and what remains open. Update the "Completed" and "Open" sections as
work lands; do not restate the invariants as new findings.

## 1. Verified project facts

- Repository `IxView`, branch `main`, Windows 11, MinGW/ucrt64 toolchain, Qt 6.11.2
  from `.deps/sdk/ucrt64` (add `.deps\sdk\ucrt64\bin` to `PATH`).
- CMake + Ninja, static libraries `ixcore` (codecs, storage, catalog, pipeline)
  and `ixui` (MainWindow, dialogs, ImageView), thin `IxView` executable, lazily
  loaded plugins `ixarchive`, `ixrawpreview`, `ixpdf`, optional `ixextensions`.
- Validation: `python scripts/validate.py` (~236 s) runs Python tests and ctest
  and writes `build/validation-summary.json`. Direct: `ctest --test-dir build
  --output-on-failure`.
- Suites: `ixview_tests` (GUI/workflow), `ixview_large_image_tests` (codec and
  storage behaviour), quality/benchmark suites, 47 Python tests.
- Baseline at commit `9aee79b`: 9/9 ctest suites (236.2 s), 47/47 Python tests
  (13.1 s), `success: true`.

## 2. Invariants — do not weaken while fixing anything

- Mapped image buffers: owner-only permissions, UUID file names, deletion moved
  to a single deleter thread, startup sweep for leftovers, unmap before delete.
- Preview/thumbnail cache: decode de-duplication with `shared_ptr` state,
  generation counters plus `QPointer` lifetime guards; cancellation is
  cooperative and never preempts a running native read.
- File publication: atomic no-replace (`RENAME_NOREPLACE`, `renameat2`,
  `renameatx_np`, `link()+unlink()` fallback), source revision compared before
  and after, `QSaveFile` staging with a re-check before `commit()`.
- Archive and extension hardening: path-traversal rejection, per-entry size caps,
  CRC validation, HTTPS-only downloads, SHA-256 verification, staged rename.
- Join semantics in `src/ui/worker_wait.h`: joins run a real event loop so paints,
  timers and input stay alive; reentrancy is blocked by `DocumentActivity`
  (`busy()` in every command dispatch) and by `closeEvent` ignoring a close while
  a transaction owns the document. Pure progress windows reappear when dismissed;
  cancellable ones stay hidden.
- Codec contract (`docs/ARCHITECTURE.md`): a codec owns the full document contract
  (pixels, path, format, page fields, `fileBytes`, metadata, `sourceHasExif`).
  Decoders must write through `QImage::bytesPerLine()`; library pitch `0` means
  tightly packed. `CodecRegistry::read` keeps the first accepting codec in charge,
  including its failure, and conflicts are resolved inside that codec.
- Every `IX_TEXT` string needs a row in `resources/languages.json` (five columns:
  de/en/ko/ja/fr, `%1` placeholders preserved); `test_localization.py` enforces it.

## 3. Completed in this session (committed and validated)

| Commit | Change |
| --- | --- |
| `cd9627d` | GUI joins keep paints/timers/input alive; saves cancel before commit |
| `317389c` | Save progress window has a working cancel button; joins stay input-capable |
| `8212149` | F3: status bar names the running transaction; rejected commands explain themselves |
| `9aee79b` | Four audit fixes: row stride, `fileBytes`, `accepts()` size limit, in-codec Qt retry |

Detail on the last commit: the fast path decodes into `Format_RGB32` with
`TJPF_BGRX` on little endian and an explicit stride, so widths that are not a
multiple of four no longer shear rows (previously silent corruption, including in
batch exports). Peak memory drops from ~7 B/px to the 4 B/px the size guard
already assumed. `finishDocument` takes the size from the already-open file.
`accepts()` mirrors the 256 MiB in-memory limit of `read()`. A decode-time
refusal retries Qt's tolerant reader inside the claiming codec, and a registry
test pins "first accepting codec owns the outcome".

Earlier in the same series: `inspectJpegHeader()` replaced the bounded byte scans
with a real JPEG segment walker (ICC detection is no longer limited to the first
64 KiB, and APP0 density is found regardless of position), and the storage pool is
a deliberately leaked heap pool to avoid static-teardown ordering.

Note on validation logs: `python-tests.log` contains a line
"python-tests failed; see ..." produced by
`test_first_open_diagnostics.test_failed_process_preserves_partial_report_and_checkpoint`,
which deliberately runs a failing validator subprocess. The authoritative result
is "Ran 47 tests ... OK" with returncode 0.

## 4. Open findings (source-verified, not yet fixed)

- **AUD-5 (P2)** — two hard blocking joins. `src/ui/dialogs.cpp:459-461`
  (histogram) and `:604-608` (batch/pipeline result) call
  `watcher.waitForFinished()` after `dialog.exec()` has already returned: no event
  processing and no progress indication while the last file finishes. Route both
  through `ix::waitForGuiWorker()`.
- **AUD-6 (P3)** — catalog serialisation. `src/core/image_catalog.cpp:22` holds a
  file-scope `QMutex` and `:187-190` waits up to 5 s for the cross-process
  `QLockFile` while holding it, around a JSON read of up to 256 MiB. Callers are
  workers (`library_dialog.cpp:233`, `:265`, `:351`), so a GUI stall is unconfirmed;
  the risk is multi-second waits under contention. Also drop the dead
  `image_catalog.h` include in `src/ui/main_window.cpp:16`.
- **AUD-7 (P3)** — OCR input permissions. `src/core/recognition_input.cpp:29-48`
  writes `input.ppm` and copied `*.traineddata` into
  `AppStorage::cacheRoot()/tool-jobs/run-XXXXXX` without tightening permissions,
  unlike the swap directory (`image_storage.cpp:200` uses
  `ReadOwner|WriteOwner|ExeOwner`). A crash leaves pixels behind. Add the same
  permissions plus a startup sweep.
- **AUD-8 (P3)** — histogram churn. `src/ui/dialogs.cpp:441-456` allocates
  `image.copy(0, y, width, 1).convertToFormat(Format_ARGB32)` per row (thousands
  of allocations on a 25 MP image). Use `constScanLine` with one scratch row.

## 5. Documented limitations — keep as contracts, do not "fix" by weakening

- Cancelling a save stops the commit, not the encoder: the running encode finishes
  and is discarded. The progress window closes immediately while the main window
  stays busy until the worker returns. Cancelling mid-encode would need a cancel
  token through `QImageWriter`/libjpeg/libwebp.
- Rejected commands are explained, not queued. Only `pendingNavigation_` and
  `deferredOpen_` queue work deliberately.
- Fullscreen shows no busy hint (the status bar is hidden there); only the wait
  cursor.
- `accepts()` reads up to 256 KiB of header per JPEG, and each fast-path JPEG sees
  three header passes (QImageReader for orientation, `inspectJpegHeader`,
  `tjDecompressHeader3`).
- No timing test proves "the GUI does not block" during swap teardown; only the
  deterministic flush test exists.

## 6. Next steps, in order

1. AUD-5: replace both hard joins with `waitForGuiWorker()`.
2. AUD-6: shorter catalog lock timeout, do not hold the in-process mutex across the
   file-lock wait, drop the dead include.
3. AUD-7: owner-only permissions on `tool-jobs` plus a startup sweep.
4. AUD-8: scratch-row histogram loop.
5. Optional: if more fast-path codecs are added, revisit a registry-level fallback
   contract. Today the registry keeps the first accepting codec and the codec
   retries internally.

## 7. Working rules for this repository

- Tests must assert an external expectation (compare against Qt/`QImageReader`),
  never the implementation's own output. The width 41/43 corruption survived a
  44/44 green suite because the only fast-path test used 40×30, whose stride
  happened to match the pitch.
- Prefer seams (`spillThresholdBytes`) over disabling code in tests; do not
  silently `QSKIP` coverage.
- One finding per commit; the message states the failure mode and the contract it
  protects. Run the full validation before committing.

