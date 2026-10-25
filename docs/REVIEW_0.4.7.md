# Directory-job review — 0.4.7, 2026-10-25

This pass addresses the uncancelled directory scans recorded in [0.4.6](REVIEW_0.4.6.md#remaining-limits). MainWindow previously submitted `scanDirectory` with a null cancellation token. New folder/sort requests set a pending-refresh flag and waited for the entire previous enumeration and sort to return before replacing its result.

## Change

`core/directory_index_service.*` owns one low-priority worker, its cancellation token and one optional pending request. Requests contain captured directory/filter/sort/latest-file values. New requests cancel the active scan and replace the pending request. Cancelled successes and failures are discarded before delivery. The service never captures a window in its worker, supports synchronous reentrant result consumers, and cancels/joins work during destruction.

MainWindow retains directory watching, the installed file list, file-position lookup and navigation decisions. Its former watcher and pending/loading bookkeeping are removed. An unchanged directory still reuses its index. `scanDirectory` also checks cancellation before starting the sort, in addition to its enumeration/output checks.

The standalone GitHub Updater is now a separate project. `.github/github-updater.json` remains with üxzView and supplies the same public-file selection to source archives and the external updater. The viewer does not import or depend on that tool.

## Verification

The targeted Release/Offscreen workflow run passes **8 checks**, including setup/cleanup and six new cases (`build/directory-index-targeted.txt`):

- Semaphores hold an active request while two replacements arrive. Only the active and final scanners run; the active scanner observes cancellation and only the final snapshot/positions are delivered.
- Explicit cancellation discards both the active result and the pending request.
- Destruction supplies cancellation and joins a running scanner before returning.
- Result consumers can synchronously request another scan or delete the service.
- A scanner exception reports failure and a subsequent request still succeeds.
- A real MainWindow uses the service and reuses its index when reopening a file in the same unchanged folder.

The Windows 11 Release build finishes without compiler warnings. The final `validate.py --suite all --require-assets` run passes all **20 steps**, including **154 functional + 13 quality + 12 storage + 17 extension + 40 workflow = 236 Qt checks**, **27 Python tests**, native window/navigation checks and 100/125/150/200 percent display scales. No tests are skipped. Evidence: `build/directory-index-build.log`, `build/directory-index-version-build.log`, `build/directory-index-validation.log` and `build/validation-summary.json`.

The SDK-free portable runtime repeats all **236 Qt checks without failures or skips**, reports application version **0.4.7**, decodes the PNG smoke fixture and runs the bundled QR/OCR engines with all five OCR models. Relocated profile/tool startup passes and temporary test binaries are removed. Evidence: `build/directory-index-deploy.log`, `build/directory-index-portable.log` and `build/portable-*-results.*`.

No C++ implementation changed after the complete native validation. The existing dependency-source inventory is reused only after checking all runtime/SDK/recipe hashes and its normal 24-hour freshness gate. The canonical archives and checksums are validated against the working sources and deployed files.

## Limits

These are deterministic orchestration checks, not a measured speedup on large folders or network drives. Cancellation is cooperative: an in-progress filesystem call or standard-library sort may complete before the token is observed. Destruction can wait for such work. There is no interruptible kernel-I/O or comparator-level sort cancellation.

Loading/saving/navigation coordination still largely belongs to MainWindow. Shared RAM accounting, journal crash recovery, atomically conditional external file operations and further package-size reduction remain open. Native Windows 10/Linux/macOS, calibrated-monitor/HDR and broad camera/OCR coverage are not established by this change.
