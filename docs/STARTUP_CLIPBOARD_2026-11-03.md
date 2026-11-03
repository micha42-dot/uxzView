# First-session JPEG opening and clipboard availability — 2026-11-03

## Report and diagnosis

The supplied `DSCF0313.jpg` is a sequential 8-bit RGB JPEG, 6000 x 4000 pixels,
3,531,479 bytes, with EXIF, XMP and an ICC profile. It is read only; no copy of
the private photograph is included in the repository or release.

Three fresh native Windows benchmark processes measured the first open in an
already-running empty window at 213–315 ms through the first observed Qt paint.
Full-image decoding accounts for most of that work; metadata reads are normally
about 1 ms and no additional color conversion was required with the supplied
profile. These measurements use warm OS file caches and isolated settings.

A separate full-start test launches the actual application with no SDK paths,
an isolated portable profile and selected read-only user display preferences.
It polls for a visible process-owned window and then the image filename in its
title. The first uninstrumented run took 4.92 seconds through the title change,
with the window appearing after 4.69 seconds. Subsequent runs took 0.41–0.54
seconds. The image worker in the slow run still completed in about 210 ms.
The title observation is document installation, not compositor paint.

The initial slow run predates detailed startup checkpoints. Its exact cause
cannot be inferred from later warm runs. Windows loader/file-cache/font/provider
work before the image request remains distinct from JPEG decoding; no claim is
made that the original 4.92-second outlier is completely explained or fixed.

## Confirmed blocking path

`MainWindow::refreshState()` previously called the Qt clipboard MIME interface
up to three times simply to enable Paste. Under Windows this can contact an
external OLE clipboard provider and retry while the clipboard is locked. It
occurs during window construction and later document/selection state updates,
even when no paste is requested.

A controlled test holds the existing system clipboard open for 1500 ms in a
test thread without reading, replacing or clearing its contents. Five fresh
actual application starts reproduce repeated Qt clipboard-retry messages and
wait inside the main-window constructor. This is a separate reproducible delay,
not proof that the user's original intermittent event had the same cause.

Windows availability checks now use advertised Win32 formats only. Bitmap/DIB,
PNG, file drops, Qt image MIME data and the URL formats used by Qt are covered.
No clipboard opening, image decoding or external payload rendering is requested
for command-state updates. Actual Paste continues through Qt, and busy/modified
document safeguards remain in place. Other platforms and offscreen tests retain
Qt MIME availability checks, using a single MIME pointer.

The format mapping was checked against the [Qt Windows MIME implementation](https://raw.githubusercontent.com/qt/qtbase/6.8/src/plugins/platforms/windows/qwindowsmimeregistry.cpp).
Microsoft documents format availability as the mechanism for enabling Paste in
its [Win32 API reference](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-isclipboardformatavailable).

## Optional startup checkpoints

`IX_PROFILE_IMAGE_LOAD=1` now also logs `ix-startup` checkpoints from entry into
`main()` through QApplication, appearance, arguments, storage, memory policy,
extensions, language/theme, window construction/show and initial open submission.
The clock reports per-stage and cumulative time; DLL loading before `main()` is
outside that clock. Existing worker/codec/install checkpoints remain available.
The environment flag is off by default and adds no UI or persistent setting.

For an intermittent reproduction, launch from PowerShell with diagnostic stderr
enabled and retain the log after closing the application:

```powershell
$env:IX_PROFILE_IMAGE_LOAD = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
.\uxzView.exe 'C:\path\image.jpg' 2> startup.log
Remove-Item Env:IX_PROFILE_IMAGE_LOAD, Env:QT_FORCE_STDERR_LOGGING
```

Logs can contain local filenames from decoder errors; review them before sharing.
No antivirus exclusions, system cache flushing or security-setting changes are
part of these measurements.

## Controlled measurements and focused checks

Windows 11, Core i5-10210U, approximately 16 GiB RAM, Release GCC 16.2 and
Qt 6.11.2. Five fresh actual application processes per arm, before followed by
after; selected user display preferences are copied into an isolated portable
profile. No SDK directories are present in PATH. Caches/CPU frequency/background
activity are uncontrolled. Medians, including DLL loading and native window setup:

| Clipboard condition | Visible window | Image installed (title observation) |
| --- | ---: | ---: |
| Held 1500 ms, previous Qt availability query | 1591 ms | 1782 ms |
| Held 1500 ms, native format query | 230 ms | 437 ms |
| Unheld, native format query | 264 ms | 511 ms |

The held test no longer waits for clipboard release. The unheld measurement is
a control, not evidence of a universal startup speedup; the two after arms vary
with normal machine scheduling. This does not claim to eliminate every possible
first-session cold-start delay. Data and hashes:
`build/dscf-startup-{locked-before,locked-after,unlocked-after}.json` and
`build/dscf-startup-summary.json`. The frozen traced baseline executable SHA-256
is `9aaa52dda3509433a76f14a5c1e056a4facf5c5d1dba69171d4cc3b35678b345`;
the measured corrected executable is
`8c924b991c799e8fcdebcae4db6e8ddd35d40e9f5630c1e3ec9a1d34114b8556`.

Five data-driven cases verify empty/text/image/local-URL/remote-URL availability,
no payload retrieval during command updates, actual image paste and later
disabling after plain-text replacement. The native image case also holds the
clipboard in a separate thread and verifies that availability returns while
the lock is still held. A three-second fallback bounds regressions. All seven
focused cases (including setup/cleanup) pass in both native Windows and offscreen
backends: `build/dscf-clipboard-native.txt` and
`build/dscf-clipboard-offscreen.txt`. The offscreen run exercises the Qt fallback;
the locked Windows branch is explicitly verified in the native run.

The initial restricted/sandboxed native-window diagnostic timed out after
40 seconds with repeated clipboard-access failures. Its partial log is retained
in `build/dscf0313-first-open-before.json`; it is excluded from native timing
comparisons. Repeating one bounded restricted process after the correction
passes all three opens: `build/dscf0313-restricted-startup-after.json`. This is a
failure-path check, run alongside other tests, not a performance sample.

Re-run the focused native regression from a configured Windows developer build:

```powershell
./build/ixview_ui_quality_tests.exe pasteAvailabilityDoesNotReadPayloads -platform windows
```

## Complete regression

The measured corrected Release executable passes all nine required-asset Qt
suites: 718 cases, no failures or skips, including 56 UI-quality cases and 152
image-quality cases. All 47 Python tests pass. Evidence:
`build/review-startup-clipboard-final-validation-summary.json`,
`build/review-startup-clipboard-final-binary.json` and `build/ctest.log`.
RAW and recognition fixtures are required. Native Windows 10/Linux/macOS
confirmation remains separate from these local Windows 11 checks.

The deployed runtime passes the same 718 Qt cases without SDK paths, failures
or skips, plus real QR/OCR engines, all five OCR languages, relocated portable
profiles and native module loading. Its executable matches the measured and
validated SHA-256 above. Evidence: `build/startup-clipboard-runtime-summary.json`,
`build/startup-clipboard-portable.log` and `build/portable-*-results.txt`.
Temporary runtime test executables and QtTest are removed after verification.
