# Development workflow

Build from [PLATFORMS.md](PLATFORMS.md) and read [ARCHITECTURE.md](ARCHITECTURE.md) before changing ownership or persistence. The supported scripts use Ninja single-configuration builds: `build` on Windows, normally `build/native` elsewhere.

## Choose the component

| Change | Start here | Regression evidence |
| --- | --- | --- |
| Decode/metadata/color | `core/image_loading.*`, `codec_registry.*`, `metadata.*`, `processing.*` | Small fixtures, precision/ICC/alpha comparisons, quality suite. |
| View/selection/fullscreen | `ui/image_view.*`, `platform_support.*` | Pixel/fit checks and native DPI/window tests. |
| Loading/navigation/history | `ui/main_window.*`, `document_activity.h`, `directory_index.*` | Queued navigation, stale results, failed reads, unsaved-edit policy. |
| Save/files/batch | `main_window_files.cpp`, `file_journal.*`, `batch_pipeline.*` | Conflicts, cancellation, rollback, source preservation. |
| Recognition | `recognition_input.*`, `tool_store.*`, `tool_dialog.cpp` | Selection/alpha export, real process, cancellation, removal/leases. |
| Packages/storage | `app_storage.*`, `extension_storage.*`, `scripts` | Corrupt/legacy state, relocation, privacy, archive integrity. |

Qt suites are in `tests/test_ixview.cpp`, `quality_ixview.cpp`, `storage_ixview.cpp`, `extensions_ixview.cpp` and `workflow_ixview.cpp`. The workflow suite covers navigation, modal file operations and related preview/export regressions. Add tests to the suite that owns the behavior. For a substantial new component, prefer a dedicated test source/target over growing the main test translation unit indefinitely. Test observable results and failures rather than mirroring implementation branches.

## Build and verify

Wait for builds to finish before running tests. UI suites run sequentially and may move/focus windows; avoid competing desktop tests.

```powershell
$env:PATH="$PWD/.deps/sdk/ucrt64/bin;$env:PATH"
cmake --build build --parallel 4
python scripts/validate.py --suite core --require-assets
```

`--require-assets` makes missing real RAW fixtures/tool packs fail instead of skip. Prepare these explicitly with `scripts/fetch_quality_fixtures.py` and `scripts/build_tool_packs.py`. The validator never downloads, builds or installs. Core validation runs Python checks and CTest; logs and `validation-summary.json` record commands, platform, return codes, timings and success. Without required assets, inspect skips in individual Qt reports.

On an interactive Windows desktop:

```powershell
python scripts/validate.py --suite native --require-assets
# Or run core and native checks in order:
python scripts/validate.py --suite all --require-assets
```

This covers recognition, document/file transactions, toolbar/selection behavior, fullscreen navigation and DPI at 100/125/150/200%. It replaces the local-only `build/run-native-*.py` helpers. The Windows 10 workflow uses the same runner after verifying the actual OS version. Windows Server CI does not establish Windows 10 desktop compatibility.

On Linux/macOS, with the configured native toolchain:

```sh
python3 scripts/validate.py --build-dir build/native --suite core
```

The Windows native suite is unavailable there. Perform the platform checks in the build guide and record the desktop, Qt version and gaps. Native Linux/macOS runs have not yet been established locally.

Targeted regressions can run directly:

```powershell
./build/ixview_tests.exe previewCancellationDropsQueuedWork -platform offscreen
./build/ixview_extension_tests.exe recognitionPreparationPreservesPixelsAndFiles -platform offscreen
python -m unittest discover -s tests -p "test_release_validation.py" -v
```

## Diagnose and measure

Toolbar artwork lives in `src/ui/icons.cpp`: original 24-unit vectors with rounded 1.65-unit strokes and roughly 18-unit silhouettes. Keep counters open at the smallest 20 px setting; distinguish related commands by shape, not color. Use the desktop palette for active/disabled states and retain the DPI-aware pixmap cache. Bump its artwork revision when changing paths. The existing `outlineIconsAndZoomSemantics` check produces `tests/output/icon-set.png` in light/dark colors and checks DPI variants; inspect it alongside the `renderUi` toolbar capture. Keep icon keys stable so action wiring and accessibility names remain intact.

- Preserve a small reproducible fixture and record dimensions, format, profile and the triggering command sequence.
- For stale results/lost edits, inspect the activity phase, worker mode and generation. Fix ownership/transition problems instead of merely hiding a command.
- For fullscreen timing, set `IX_PROFILE_TRANSITIONS=1` for a native test, then unset it. Logs include snapshot, cover-ready, restore and total times. Compositor timing is not a visual guarantee.
- Use `ixview_benchmarks` and [PERFORMANCE.md](PERFORMANCE.md) for speed work. Retain before/after builds, alternate equivalent runs, distinguish cold/warm caches and avoid simultaneous builds/tests. Report regressions too.
- Keep generated logs/fixtures in `build` or `tests/output`; do not commit private images or profiles.

## Workspace housekeeping

Keep the configured build, installed SDK, custom HEIF/AVIF builds, tool-pack build inputs and real RAW fixtures for fast incremental work. Disposable outputs include old probe executables/scripts, duplicate test checkouts/install trees, generated benchmark image folders and abandoned test directories. Retain reports/raw measurements that support published findings. SDK binary-package downloads in `.deps/cache` can be discarded after installation; `bootstrap_sdk.py` downloads missing packages again. Keep pinned third-party source archives and the current release assets, including optional packs under `dist/tools`. Personal portable profiles, tools and settings are not build debris. Verify resolved paths before recursive deletion and never follow links out of the workspace.

## Release and dependency changes

`scripts/prepare_release.ps1` builds the seven pinned tool packs before Windows deployment. Ordinary Windows CI also prepares these packs. Canonical outputs are `dist/uxzView`, `dist/uxzView-win64.zip` and `dist/uxzView-source.zip`; no nested release directory is needed.

After runtime changes, redeploy and run `scripts/verify_portable.py`. It checks the bundled libraries without SDK paths and exercises moved portable storage/tools. Refresh source verification using `scripts/dependency_sources.py --check-urls`, then use `scripts/package.py --release`. Do not package while portable verification has temporary test binaries installed.

For new dependencies, update lock/recipe, license/source inventory, deployment closure and size budget together. Recognition engines stay in separate processes and start only on explicit use. Format modules follow existing compatibility rules. Publish binaries as release assets.

## Finish a change

Describe the concrete behavior and actual verification in the PR. Update the focused documentation when a storage key, protocol, command or build step changes; keep README focused on users. Record unfinished structural work in the architecture map/critical review. Do not claim native platforms or image variants passed when only synthetic/offscreen checks ran.
