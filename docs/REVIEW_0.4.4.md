# Review for 0.4.4 — 2026-10-24

This pass examines preview cancellation, cached RAW loading and slideshow navigation. It follows the [earlier review](REVIEW_2026-10-24.md); it is not a claim of exhaustive platform or codec coverage.

## Reproduced defects

| Priority | Trigger and previous behavior | Correction |
| --- | --- | --- |
| P2 | Hide the neighbor strip before its preview completions arrive, then show it without changing the image. The shared file list/current index matched, so cancelled requests were never restarted and placeholders remained. | Mark cancelled previews as needing refresh; restarting the same list retains buttons and reissues their requests. Changes to hidden preview counts also retain this refresh requirement. |
| P2 | Reopen a RAW whose original pixels are already cached. The load pipeline still extracted/published a smaller embedded preview before checking the original cache. | Run optional preview work only on a full-image cache miss, outside the cache mutex. Keep the revision check around the complete read so a changed source cannot be installed as the captured revision. |
| P2 | Manually choose a queued image during a random slideshow. That same path could be the next automatic choice. | Skip the current path while popping shuffled candidates. Repeat mode begins a fresh round in the same call when necessary; no per-tick full-folder filtering is added. |

All three regressions failed against 0.4.3: `build/review-044-before.txt`. Their source is `tests/workflow_ixview.cpp`; the RAW case uses the pinned Pentax DNG fixture and fails for missing assets when `IX_REQUIRE_RAW=1`.

## Performance and verification

The cached-RAW benchmark compares the former preview-first sequence with the current pipeline in five alternating samples, both with the default 16 MiB preview cache and with preview caching disabled. Full-image caching remains enabled; color conversion is included, GUI delivery/painting is excluded. Every result must retain the cached original pixels and match the reference display pixels. It does not measure first RAW development or images too large for the original cache.

```powershell
./build/ixview_benchmarks.exe tests/output/raw-cache-044.json --profile-raw-cache -platform offscreen
```

On Windows 11 with Qt 6.11.2, median times were **0.415 → 0.202 ms** with the default preview cache and **64.867 → 0.216 ms** with preview caching disabled. The absolute saving in the default configuration is small; the disabled-cache case avoids repeated embedded-preview extraction. Preview deliveries fell from five to zero in both cases. All original/display comparisons passed. See [performance notes](PERFORMANCE.md#cached-raw-loading-in-044-2026-10-24) for scope and reproduction.

The Release build completed without compiler warnings. `scripts/validate.py --suite all --require-assets` passed all **20 steps**: **24 Python tests**, five Qt suites (**154 functional + 13 quality + 12 storage + 17 extension + 11 workflow = 207 checks**, including setup/cleanup), native Windows workflows and 100/125/150/200 percent scaling. No Qt cases were skipped. Local evidence is in `build/validation-summary.json` and its named reports. The three added regressions also pass independently in `build/review-044-after.txt`.

`scripts/verify_portable.py` repeated all **207 Qt checks without failures or skips**, with the SDK removed from `PATH`. The installed app reports 0.4.4 and decodes the PNG smoke fixture; bundled QR/OCR engines, all five OCR models and relocated portable-profile startup passed. Evidence: `build/review-044-portable.log`, `build/portable-*-results.*` and `build/portable-storage-startup.json`. Temporary test executables and Qt Test were removed afterward.

The canonical Windows/source archives remain directly in `dist`. Release validation checks matching build/runtime artifacts, the unchanged dependency DLL hashes, SDK lock and source inventory. No new runtime dependency was introduced.

## Remaining priorities

- Directory scans already support cancellation, but the main-window scheduling does not yet pass a cancellation token. Large obsolete scans can delay a newer scan. A load/index coordinator and shared memory accounting remain useful next steps.
- Save conflict checks and filesystem commits are not atomic against unrelated editors. Automatic file-journal crash recovery remains incomplete.
- Force-refreshed directory snapshots currently recreate neighbor widgets even when paths match. Avoiding that reset requires preserving revision revalidation; normal image navigation already reuses overlapping buttons.
- Fullscreen behavior still needs broader GPU/monitor observation; geometry tests alone do not establish flicker-free presentation. Native Windows 10/Linux/macOS, broad RAW/metadata/color and OCR-language accuracy remain open.

0.4.4 is a maintenance release. The existing settings, tool protocols and storage identifiers remain compatible.

## Resize dropdown and Korean rendering — 2026-10-25

Pixel replication / pixel art is now the seventh entry in the regular resize-filter dropdown. The separate radio-button method selector was redundant. Selecting Pixel-Art keeps the dropdown usable, suppresses sharpening/gamma/fast-downscale processing and restores those controls when returning to a smooth filter. The existing resize-dialog regression checks every filter's mapping and preservation of the downscale preference.

Windows startup now applies a language-aware UI font after loading the selected language. Korean uses Malgun Gothic, Japanese prefers Yu Gothic UI, and other platforms retain system fallback behind appropriate CJK families. Legacy dialogs share that policy, with a minimum 9-point size and antialiased sans-serif preference for Korean/Japanese. No font is downloaded or bundled. The resize dialog has enough horizontal space for its longer preset names.

The native Korean workflow passed, including installed Malgun Gothic resolution and actual Hangul glyph coverage. Settings and Resize captures were inspected for rendering and clipped text. This does not establish rendering on every installed font set or replace native-language translation review. Local captures: `tests/output/korean-settings-font.png` and `tests/output/korean-resize-font.png`.

The first complete run exposed a separate test-only issue during the repeated local hour at the end of daylight saving time: restoring a local file timestamp chose a different UTC offset. The same-size cache-rewrite regression now reads/restores/compares the timestamp in UTC. Its content-invalidation assertions are unchanged. The original failure remains in `build/dropdown-font-before-utc.txt`; the corrected test, resize selection and existing pixel/language checks pass natively in `build/dropdown-font-targeted.txt`.

Final revalidation passed all **20 public validation steps** and **24 Python tests**. Both the development and SDK-free portable builds pass **154 functional + 13 quality + 12 storage + 17 extension + 12 workflow = 208 Qt checks**, without failures or skips. The additional native Korean-font probe passes at 100/125/150/200 percent scaling. Reports: `build/dropdown-font-validation.log`, `build/validation-summary.json`, `build/korean-font-scale-*.txt` and `build/dropdown-font-portable.log`. The shared test reports now contain this newer UI pass; the 207-check counts earlier in this document describe the preceding 24 October snapshot.
