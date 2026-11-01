# Reliability and performance review · 1 November 2026

Version 0.5.0, changes recorded under Unreleased. Local work is on
`codex/reliability-2026-11-01`; no push, vendor submission or public upload was
performed. README image markup and both image assets are unchanged from the
starting commit `4690a24`. Discarding unsaved edits remains the default.

## P1 and P2 dispositions

Each row has its own local commit, complete build and successful required-asset
core validation. Changes include a regression where useful; trivial settings
reader consolidation relies on the existing load/navigation suites.

| Finding | Result and principal files | Commit |
| --- | --- | --- |
| P1.1 unchecked channel indices | Reject invalid RGB indices before indexing; tests include extreme indices, alpha and 16-bit input. `core/processing.cpp`, quality tests. | `2026234` |
| P1.2 missing menus | Recreate missing root menus with localized titles; ignore a missing crop submenu safely. `ui/menu_support.h`, `main_window.cpp`, UI quality tests. | `18ac8b2` |
| P2.3 automatic crop cost | Scan native 32-/64-bit rows without another full-image buffer; compute background channels once. Other formats retain Qt pixel semantics. `processing.cpp`, scalar reference and benchmark harness. | `39c0577` |
| P2.4 image working sets | Guard estimated source/working/output image bytes before allocation; release histogram/filter inputs before conversion. Reject invalid kernel geometry and null conversion output. `processing.cpp`, quality tests, translations. | `ae0fde2` |
| P2.5 repeated catalog rewrite | Collect ordered classic-batch relocations and flush one catalog update, including completed moves on cancellation. Serialize process-local file operations through the flush; preserve unrelated annotations. `file_operations.*`, `image_catalog.*`, `batch_processor.cpp`, file-operation tests. | `56159c0` |
| P2.6 POSIX overwrite race | Use no-replace publication; cross-volume moves copy to private destination-volume staging before publication. Fail safely without a supported primitive. Windows keeps its existing path. `file_operations.cpp`, file-operation tests. | `3a46f52` |
| P2.7 worker progress | Document concurrent/out-of-order callbacks and test actual worker callbacks versus deferred GUI delivery. Preserve PipelineDialog's existing guarded/coalesced mailbox. `batch_pipeline.h`, quality tests. | `da94caa` |
| P2.8 GUI stalls | Read/write catalog snapshots and enumerate folders on workers; keep paints/timers alive during mandatory joins. Own library inputs, filter existing extension rows and remove the unused eager thumbnail dialog. `worker_wait.h`, `library_dialog.cpp`, `main_window.cpp`, `dialogs.*`, extension manager and UI/workflow tests. | `8d6730b` |
| P2.9 settings readers | Capture load preferences with one settings reader. Qt already caches backends; no claim of eliminating two full INI parses. `main_window.cpp`. | `14b8422` |

The crop implementation deliberately avoids the proposed unconditional RGBA64
conversion. That conversion adds a full image and can change palette,
premultiplied or floating-point comparisons. Nine format cases compare the
production result with the retained scalar reference. `perPixel` originally
had one RGBA64 working buffer, not two; the new budget also includes final
conversion where needed. The 512 MiB processing budget is an operation-local
estimate of image buffers, not a cap on total process memory, decoder state,
auxiliary allocations, caches or concurrent jobs.

## P3 dispositions

| Finding | Decision |
| --- | --- |
| Temporary/stage naming | Retain existing names. Cosmetic renaming adds no speed benefit; persistent lock/profile names also participate in compatibility. |
| Success versus error | Add a distinct warning field. Direct/batch moves remain successful when only catalog relocation fails; UI and logs display the warning. Commit `ef23ff4`. |
| DPI conversion overflow | Reject nonfinite/out-of-range EXIF rationals before casting, and adapt the denominator for representable high DPI. Test normal values, invalid values, high-DPI saving and unchanged source bytes. Commit `1a8a6e0`. |
| Destination hash cap | Retain streamed, cancellable hashing for eligible replacements. The 64 MiB source restriction is for watermark resources, not input images. Capping target hashes would weaken review integrity. |
| Tool-state cache | Keep fresh state for integrity/install/removal operations and leases. Search now filters existing rows; an unscoped cache would risk stale removal/activation state. |
| Static format exclusions | Retain the documented restart requirement in `PLUGIN_MANAGER.md`; active libraries are not unloaded mid-session. |
| Shared smoothing key | Separate `view/smoothFit` and `view/smooth`, migrate old profiles, cache both choices for rendering. Pixel comparisons test fit/manual transitions and persisted independent actions. Commit `0d2b694`. |
| Delete target from stale index | Resolve the source path, skip duplicate source entries, handle absent/unknown indices and normalized Windows-case aliases. Pure target tests do not touch the user's trash. Commit `1a7eca7`. |
| Hotfolder versus queued steps | Let queued manual navigation win over automatic latest-image opening. A controlled directory snapshot exercises the race. Commit `1a7eca7`. |
| File-open window capture | Use a guarded QPointer for OS callbacks. Commit `1839fd5`. |
| Failed watch registration | Report hotfolder registration failure and leave its action unchecked; ordinary indexing retains manual Refresh and logs an unavailable watch. Do not rescan on every arrow press after a quota failure. Commit `1839fd5`. |
| Completed future style | Consume preview/save results once; required file-worker joins process paints/timers and retain a shutdown join. Commit `1839fd5`. |

## Measured automatic crop improvement

Windows 11, Qt 6.11.2, generated 6000 × 4000 image already in memory. Three
alternating calls per implementation/format in one process, identical crop
bounds; medians below. Generation, decoding and file IO are excluded.

| Input | Scalar reference | Native scanlines |
| --- | ---: | ---: |
| RGB32 | 907.03 ms | 87.06 ms |
| RGBA64 | 534.31 ms | 122.12 ms |

This is a component measurement with three samples per arm, not a cold-start,
whole-viewer or peak-RAM benchmark. The optimized path adds no converted full
image. Reproduce with a fresh output path:

```powershell
./build/ixview_benchmarks.exe build/auto-crop.json --profile-auto-crop -platform offscreen
```

Evidence: `build/review-auto-crop-final-profile.json`. Catalog tests also verify
one flush and unchanged unrelated records; no aggregate batch speed percentage
is inferred from them.

## Validation

Windows 11, Qt 6.11.2: all **8/8 Qt suites**, **516 Qt cases** and **45 Python
tests** pass. Required RAW/tool assets are present and no Qt cases are skipped.
The same 516 cases pass against the portable runtime without SDK paths; QR,
all five OCR models, relocated-profile startup and optional-module loading pass.

| Suite | Passed cases |
| --- | ---: |
| functional | 200 |
| workflow | 143 |
| quality | 57 |
| storage | 12 |
| file-operation | 29 |
| extension | 17 |
| maintenance | 15 |
| UI quality | 43 |

The new smoothing, post-delete navigation, hotfolder priority and failed-watch
checks also pass with the native Windows backend: 17 cases at each of
100/125/150/200 percent scaling. These are targeted automated checks, not a new
complete native matrix or manual visual sign-off.

Evidence: `build/review-p3-windows-metadata-validation-summary.json`,
`build/review-runtime-summary.json`, `build/portable-*test-results.*` and
`build/review-native-navigation-*.txt`. The documentation-only final pass is
recorded as `build/review-final-validation-summary.json`.

Each code step retained `build/review-STEP-{build,defender,validation}.log` and
its copied `validation-summary.json`. The complete final reports and runtime
checks remain local build artifacts rather than source/package contents.
Counts include Qt setup/cleanup and data rows, not independent features.

One initial catalog-suite run failed while creating an unrelated annotation;
its assertion did not retain the underlying error. The isolated case, three
complete file-operation reruns and subsequent full validations passed. The
diagnostic assertion now includes that error; no speculative retry was added.
The unexplained failure is retained in
`build/review-catalog-first-fileops.txt` and its validation summary; diagnostic
and three suite reruns are under `build/review-catalog-{diagnostic,suite-*}.txt`.
Successful reruns do not identify its cause.

## Windows identity and AV status

Preserve the supplied VERSIONINFO resource, with one correction: the declared
codepage is now 1200, matching string table `040904b0`. The new
`scripts/verify_windows_metadata.py` verifies fixed/string versions and every
declared translation through Windows APIs without launching the executable.
Release preparation runs it. Before the correction the strict query could not
find `040904e4/CompanyName`; afterward every required field resolves.

The standard Defender signature update completed. Post-update status records
engine **1.1.26100.4**, service **4.18.26100.4**, definitions
**1.463.66.0** (last updated 2026-10-31T19:13:49+01:00). Antivirus and
real-time protection are enabled. Explicit custom scans of the final build EXE
and the entire deployed directory report **found no threats**. The accessible
threat-history query returned zero matching viewer records; this does not refute
the user's earlier alert.

Final EXE SHA-256 (build and deployed copy match):

`222ddd76b87b513caa6c747b203f7d099b965c057eaa0ae5a366aea6848d6379`

File/ProductVersion: `0.5.0.0`. The PE certificate table is empty;
there is no embedded Authenticode signature. Scanner/status/version evidence is
in `build/review-{final-defender,runtime-defender,defender-status,windows-metadata}.*`
and `build/review-authenticode-presence.json`.

The supplied context reports Defender/Bitdefender-family detections, but no
independently verified VirusTotal report or vendor determination was provided.
Generic naming, resource metadata and a clean local scan cannot establish the
cause of the original alert or prove a false positive. No exclusions, protection
changes, dummy signatures, packing or detection-evasion rebuilds were introduced.
No code-signing certificate was supplied; no signing or vendor submission was
performed. Signing, timestamping, exact-hash submissions and their limits are
documented in [release preparation](RELEASING.md).

## Remaining limits

- Native Windows 10, Linux and macOS execution/compilation was not performed on
  this Windows 11 host. In particular, the new POSIX move branches still need
  native CI and same-/cross-volume filesystem runs. Windows tests do not prove
  those branches. The optional Linux cross-volume test requires two writable
  filesystems and reports a skip if unavailable.
- Physical moves and the catalog are not one crash-safe filesystem transaction.
  A crash before a batch flush can leave annotations at old paths. No journal
  or file-operation Undo was reintroduced. An interrupted no-replace fallback
  can leave two names with an error; it must not delete a possibly replaced
  destination to hide that outcome.
- Revision checks narrow external-write races; they are not a cross-process
  compare-and-swap. Blocking OS reads and third-party decoders may still delay
  shutdown; joining is retained to prevent use-after-free.
- This review does not establish calibrated-monitor accuracy, complete RAW/HDR
  coverage or absence of all defects. MainWindow still coordinates many flows;
  the extracted helpers improve specific ownership boundaries, not the whole
  architecture at once.
