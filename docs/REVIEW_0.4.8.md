# Efficiency and style review — 0.4.8, 2026-10-25

This pass targets avoidable UI work and misleading/redundant presentation after [0.4.7](REVIEW_0.4.7.md). It changes no image-processing algorithm or format dependency.

## Findings and changes

| Area | Previous behavior | Correction |
| --- | --- | --- |
| Command search | Every edited character clears and reconstructs every model item and the alias map. | Construct the optional search model once; typing changes only its completion prefix. Aliases are immutable/shared, and action changes update individual rows. |
| Command state | Search rows retain old enabled/text state until typing again, and deleted actions leave stale rows/raw pointers. | Observe action changes and destruction; persistent row indexes survive filtering and removal of other rows. |
| Keyboard invocation | Escape clears the field but leaves a matching completion; Enter in the empty field can trigger that command. | Reject empty/whitespace invocation, select an available default match and retain the existing reentrancy guard. A guarded action and an already-scheduled context timer support a consumer deleting the action/widget during `finished`. |
| Thumbnail labels | Unannotated images show `0 ★ · `; color marks display internal English identifiers. | Plain filenames for unannotated images; actual stars and localized color names only when present. |
| Layout/copy | Persistent catalog guidance, a crop tutorial, redundant current-image text and a misleading “not a file” counter during indexing. | Remove the guidance/redundant text. Keep numeric position and accessible current-image identity; unknown counters remain blank until indexed. Duplicate results have an explicit Open action while retaining double-click. |
| Repaint work | The current-preview label is destroyed/recreated during navigation; fallback icons expand to thumbnail dimensions. | Retain the label with the existing thumbnail widgets. Limit the fallback image drawing to 32 logical pixels, centered in its requested canvas; retain palette/DPI caching with a new cache revision. |
| Catalog progress | Completion of the read loop can auto-reset/hide progress before publication, and cancelled/failed indexing still reports success. | Progress remains until worker completion; cancelled/failed jobs do not show the success message. Filtering stops at the first unmatched search term. |

The command-search model is still created lazily when that optional toolbar field is enabled. Reusing rows removes per-keystroke item allocation; it adds live state subscriptions while the component exists. It is not a demonstrated general application speedup or total-memory reduction.

## Reproductions

The initial Release/Offscreen run reports **2 passed, 5 failed** against the preceding implementation (`build/style-efficiency-before.txt`): stale enabled state, invalidated rows while typing, execution after Escape then empty Enter, stale rows after action deletion, and empty/raw-language catalog annotations. The first focused corrected run passes those cases plus invocation-time owner deletion and cancelled catalog feedback: **9 checks including setup/cleanup** (`build/style-efficiency-after.txt`). Four preceding search/preview/duplicate workflows also pass (`build/style-efficiency-existing.txt`).

Additional final regressions cover Enter with an unavailable first match and both duplicate-opening paths, including rejecting the distance column for Open. Preview reuse now also checks the retained current marker. `tests/output/library-style.png` captures the controlled gallery presentation; the existing icon/render checks cover palette/DPI output.

An initial full run exposed a shared fixture filename in the new two-row duplicate test before its second UI path could execute. The rows now use distinct files; both opening paths pass their focused rerun (`build/style-efficiency-duplicate-results.txt`). The stopped run remains recorded in `build/style-efficiency-fixture-before.txt` and `build/style-efficiency-fixture-before-summary.json`.

The final Release build completes without compiler warnings. The complete Windows 11 `validate.py --suite all --require-assets` run passes all **20 steps**, including **155 functional + 13 quality + 12 storage + 17 extension + 48 workflow = 245 Qt checks**, **27 Python tests**, native window/toolbar/navigation checks and 100/125/150/200 percent display scales. There are no test failures or skips. Evidence: `build/style-efficiency-final-build.log`, `build/style-efficiency-fixture-build.log`, `build/style-efficiency-validation.log` and `build/validation-summary.json`.

After deployment completion was confirmed, the SDK-free portable runtime passes all **245 Qt checks without failures or skips**, reports version **0.4.8**, decodes the PNG smoke fixture and runs the installed QR/OCR engines with all five OCR models. Relocated profile/tool startup passes; temporary Qt Test/runtime-test binaries are removed. Evidence: `build/style-efficiency-deploy.log`, `build/style-efficiency-portable.log` and `build/portable-*-results.*`.

An earlier portable attempt failed the built-in-format state assertion in `pluginManifestArchitectureAndHashGuards`; it was dispatched before deployment completion had been confirmed. The ordered complete rerun passes, but the precise cause of that first assertion is not established. Its record is retained in `build/style-efficiency-portable-before.txt` and `build/style-efficiency-portable-before.log`; the successful rerun is not proof that all intermittent plugin/filesystem failures are solved.

No application C++ changed after the complete native validation. Dependency inventory reuse checks exact runtime/SDK/recipe hashes and the normal 24-hour source-check freshness gate. Archives and checksums are checked against deployed files and working sources.

## Further findings

- `MainWindow::updateStatus()` still queries filesystem modification time during zoom/selection refreshes. Moving this off the GUI thread needs an explicit freshness/revision contract, including Save As and file undo; a stale timestamp cache would be misleading.
- The catalog still stores whole JSON snapshots and constructs list items for all filtered matches. Visible-thumbnail decoding is bounded, but a model/delegate and an indexed store remain larger opportunities for very large folders.
- MainWindow still owns broad document/save/file-management coordination. Separate memory budgets are not a global RAM bound. Journal crash recovery and native Windows 10/Linux/macOS, calibrated-monitor/HDR and broad camera/OCR checks remain open.

Light/dark icon and native DPI checks are not a complete manual visual assessment of every dialog or GPU. This pass retains the established toolbar artwork; native-language translation review remains outstanding.
