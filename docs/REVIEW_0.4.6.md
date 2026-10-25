# Data-path review — 0.4.6, 2026-10-25

This pass examines save publication, file-journal undo, batch source revisions and resize allocation/DPI boundaries. It follows [0.4.5](REVIEW_0.4.5.md). It does not claim exhaustive decoder, filesystem or platform verification.

## Findings and corrections

| Priority | Previous behavior | Correction |
| --- | --- | --- |
| P1 | A destination rewritten, created or deleted after the application captured its overwrite decision could still be replaced/recreated by encoding. | Saving accepts an expected target revision, checks it before opening the staging file and again immediately before commit. Without an explicit expectation it captures the destination at entry. An empty expectation requires absence. The main-window save path retains its snapshot across subsequent confirmations and passes it to the encoder. |
| P2 | Undo treated an unknown journal operation like an ordinary created/copied output and could move its target into the internal trash. | Reject operation kinds outside copy/move/delete/create/replace before changing the target or the operation state. Valid existing journal identifiers remain compatible. |
| P2 | Batch sources were checked before decoding, but an intervening source rewrite during reading/processing could still publish stale pixels. | Recheck after decoding and before installation. A mismatched source discards staged output and returns an error. Configured codec registries can be supplied to the synchronous run; worker copies permit the same custom readers used elsewhere. |
| P2 | Automatic DPI could exceed the displayed 1–100000 range. Invalid or extreme values reached an unchecked floating-point-to-integer conversion for dots-per-meter metadata. | Bound derived/model DPI to the supported UI range, reject nonfinite manual values and validate processing density before allocation/conversion. The existing normal DPI conversion stays unchanged. |
| P2 | Nearest-neighbor resizing was charged for a separable-filter intermediate it never allocates. Smooth resizing omitted overlapping 16-bit input/final-conversion buffers. | Estimate image buffers for the selected method, including the sharpening peak. Retain dimension validation before size arithmetic. This is not a global process-memory guarantee. |

The initial focused run (`build/data-review-before.txt`) reports **2 passed, 5 failed** before adding the guards: three destination changes, unknown journal kind and a decoder-side source rewrite. The new optional arguments were already wired for this controlled reproduction; it is not a run of an unmodified 0.4.5 binary. The decoder fixture deliberately rewrites its source after reading the old pixels.

Additional checks cover matching existing/absent destinations with exact 16-bit pixels/ICC, a real Save As dialog whose JPEG destination changes during the alpha-loss warning, DPI bounds/nonfinite inputs, and method-specific resize allocation rejection/acceptance. The dialog check requires that the newer destination bytes survive and that the active document retains its original path.

## Allocation accounting

The allocation regression uses an RGB32 source of 10000×1000 and a 60000×100 nearest result: source/output image buffers total **64 MB**, whereas the old generic formula charged **568 MB** and rejected it. No filtered intermediate or RGBA64 input is allocated for this method.

For a 10000×2000 RGB32 smooth resize at the same dimensions, the old formula charged **400 MB**. The current conversion/filter/output path can have **640 MB** of image buffers alive before temporary filtering images leave scope, so the revised budget rejects it before allocating those extra buffers. These are buffer-size calculations and regression results, not measured peak RSS or general speedup claims. Filter weights, allocator overhead, active documents, history, caches and concurrent jobs still need shared accounting.

`imageFileKey()` moved unchanged from the preview/cache implementation into `core/file_revision.*`. Saving, loading and file operations can share native identity/timestamp checks without depending on preview workers. A byte-for-byte function-body comparison against the preceding staged implementation confirmed that native precision and identity logic were retained.

## Verification

The Windows 11 Release build completes without compiler warnings. The final workflow suite passes **34 checks**, with no failures or skips (`build/data-review-after.txt`). The public `validate.py --suite all --require-assets` run passes all **20 steps**, including five Qt suites (**154 functional + 13 quality + 12 storage + 17 extension + 34 workflow = 230 checks**, including setup/cleanup), native window/workflow checks and four display scales. Evidence: `build/data-review-validation.log` and `build/validation-summary.json`. Its original 24 Python tests passed; after adding source-refresh coverage, a separate complete Python run passes **27 tests** (`build/data-review-python-final.log`). No C++ implementation changed after native validation.

`verify_portable.py` repeats all **230 Qt checks without failures or skips**, removes SDK paths, checks the installed application's 0.4.6 version/PNG decoding and runs bundled QR/OCR engines with all five OCR models. Relocated profile/tool startup passes, and temporary test executables/Qt Test are removed. Evidence: `build/data-review-portable.log` and `build/portable-*-results.*`.

Release validation checks exact build/runtime artifacts, dependency hashes, SDK lock, build recipes and current source availability. The canonical archives stay directly in `dist`; the archive audit verifies member lists, current source/runtime bytes, embedded source ZIP and all four download checksums. No runtime dependency, font or model was added.

## Native navigation observation

The first native window-group run reported one missing Space transition after reaching the last file (`build/data-review-native-window-before.txt`). Three isolated native repetitions passed (`build/data-review-navigation-{1,2,3}.txt`), so they do not establish the cause of that single failure.

The image-view handler supported Ctrl+Space/Backspace directly, but plain Space/Backspace depended entirely on window shortcuts. They now use the same direct navigation path when delivered to the view. A separate regression checks both directions and modifier behavior without any main-window shortcuts. This closes an uncovered input path; it is not proof of the original failure's root cause.

## Release maintenance

The final packaging attempt correctly refused an expired source-availability check. `dependency_sources.py --refresh-urls` now validates the existing runtime/SDK/recipe hashes before rechecking the official source index. It permits reuse of previously proven origins without downloading discarded SDK binary archives; changed runtimes still require complete mapping. Three additional Python regressions cover success without archives, rejection before network access for a changed runtime, and preservation of inventory/notes after a failed source check. The actual refresh verified **65 MSYS2 source URLs**; the normal 24-hour release gate remains enforced.

## Remaining limits

- Revision checks followed by filesystem commits are not an atomic compare-and-swap against unrelated applications. A narrow external-write race remains between the last check and commit. Native picker confirmation internals and arbitrary remote filesystems are not controlled by these guards.
- Journal crash recovery for pending/undoPending operations remains incomplete. Corrupt or unknown records are refused; automatic recovery is a separate requirement.
- Superseded directory scans still lack the main-window cancellation token, and total RAM coordination is still missing. Individual image-buffer/cache limits are not a process-wide cap.
- Resizing retains a single DPI value for both axes; non-square pixel density and print-size precision require further design/coverage. Fixed dialog geometry and proportional behavior after swapping dimensions remain tracked in the preceding review.
- The current evidence is local Windows 11. Windows 10 Korean rendering, native Linux/macOS, network/removable filesystems, broad camera variants, calibrated-monitor/HDR output and OCR-language accuracy remain open.
