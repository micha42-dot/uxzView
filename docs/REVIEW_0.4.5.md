# UI consistency review — 0.4.5, 2026-10-25

This pass follows the [0.4.4 review](REVIEW_0.4.4.md) and concentrates on choices whose visible state disagreed with their effect. It does not establish Windows 10 rendering or exhaustive UI coverage.

## Corrections

| Priority | Trigger and previous behavior | Correction |
| --- | --- | --- |
| P1 | Screen-size buttons share a group with fixed-size presets. Their automatically assigned negative IDs reached `sizes[i]` without a bounds check. Release tests happened to complete; debug bounds assertions or invalid memory access were possible. | Reject IDs outside the fixed-size table before lookup. Screen actions apply their own dimensions. |
| P2 | Half, Double, Swap sides or a custom preset changes the pending size while percentage/megapixel mode can remain selected. | Every dimension shortcut selects absolute-size mode and refreshes input availability consistently. |
| P2 | Add a resize preset, then Cancel. The preset has already been persisted. | Stage the list in the dialog and write it only after successful OK. |
| P2 | Automatic DPI is enabled but its input stays editable; edits are immediately replaced by the derived value. Physical-size inputs also conflict with deriving DPI from pixel dimensions. | Show derived DPI in a disabled field; automatic DPI selects pixel units and disables cm/inch until turned off. Manual DPI is retained. |
| P2 | German UI still forces a decimal point. Typing a fractional size can reformat linked fields before the number is complete, e.g. 2.5 inches becoming 2 inches. | Resize, rotation and color controls inherit the UI locale. Resize numeric editors commit keyboard input before synchronizing linked fields; sliders/programmatic changes still apply normally. |
| P2 | Rotation can simultaneously promise a fast method without antialiasing and retain an active antialiasing checkbox. Disabled background preservation also leaks into returned options. | Disable/mask antialiasing for the fast method and mask background preservation unless the canvas option is active. Restore retained smoothing preferences on returning to the normal method. Use the common Cancel translation. |
| P2 | An out-of-range filter in a pipeline profile shows no selection. Saving the editor can change the effective filter, while processing separately clamps the original value. The same filter also has different names across dialogs. | Clamp the editor exactly like processing and share all seven localized filter labels with the normal resize dialog. Existing serialized IDs remain unchanged. |

The initial targeted run reports **5 passed, 9 failed** in `build/ui-consistency-before.txt`. It includes seven preset/shortcut rows; ordinary and screen presets passed while four other shortcuts failed. Additional post-correction typing checks exposed the linked-field reformatting issue and are retained. The screen-button invalid index was identified by source inspection; no release crash is claimed to have been reproduced.

`IXVIEW_VERSION` is used only by `src/main.cpp`, but was exported to all UI consumers. It is now private to the application target, reducing unnecessary UI/test recompilation on subsequent version changes. Runtime dependencies, profile IDs and storage identities are unchanged.

## Verification

The Windows 11 Release build completes without compiler warnings. The corrected workflow suite passes **24 checks**, including seven data-driven shortcut rows, with no failures or skips (`build/ui-consistency-after.txt`). Translation coverage now checks `.h` as well as `.cpp` files so the shared filter labels remain covered.

`scripts/validate.py --suite all --require-assets` passes all **20 steps**: **24 Python tests**, five Qt suites (**154 functional + 13 quality + 12 storage + 17 extension + 24 workflow = 220 checks**, including setup/cleanup), native Windows workflows and 100/125/150/200 percent display scaling. The new workflows also pass through the native Windows plugin. Evidence: `build/ui-consistency-validation.log`, `build/validation-summary.json` and its named reports.

`scripts/verify_portable.py` repeats all **220 Qt checks without failures or skips**, removes the SDK from `PATH`, verifies the installed application's 0.4.5 version and PNG decoding, runs bundled QR/OCR engines and all five OCR models, then relocates the portable profile. Reports: `build/ui-consistency-portable.log` and `build/portable-*-results.*`. Temporary Qt Test and test executables are removed afterward.

Release packaging verifies matching build/runtime artifacts, unchanged dependency origins/hashes and the SDK lock. The canonical binary and source ZIPs remain directly in `dist`; the archive audit checks exact members, current source/runtime bytes, embedded source ZIP and download checksums. No new runtime library, font or model is bundled.

## Remaining design work

- Resize, rotation and color dialogs still use fixed geometry and small legacy layouts. Converting them to layouts is preferable to adding more positional exceptions, especially for translations and accessibility fonts.
- Resize presets look like persistent radio selections, although subsequent manual edits can diverge from the selected preset. The proportional lock also references the original aspect ratio after swapping sides; the intended behavior should be made explicit before changing its model.
- Automatic DPI uses the width scale and the current controls show integer DPI. Nonuniform resizing and extreme scales need a clearer print-size/precision policy.
- Rotation still contains a large instruction block explaining hidden right-click gestures, while other dialogs expose ordinary action buttons. These gestures deserve a simpler visible interaction.
- Settings and extension pages mix immediate actions with changes committed on OK. Explicit profile Save/Delete actions are different from staging a newly added resize preset, but their scope should be visually clearer.
- Korean rendering is checked locally on Windows 11. Windows 10 font fallback, rasterization and DPI behavior remain unverified on an actual Windows 10 desktop.
- The previous architectural priorities remain: cancellation of superseded directory scans, shared memory accounting, narrower main-window responsibilities and file-journal crash recovery.
