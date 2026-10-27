# Desktop build review — 27 October 2026

## Scope

Targets: Windows x64 (Windows 10 1809+), Ubuntu 24.04 x64,
macOS 15+ arm64 and x86_64. Application version remains 0.4.8.
Windows uses the locked GCC/Qt SDK; Unix CI uses Qt 6.8.3 and
distribution/Homebrew development libraries. These are separate native builds,
not a universal binary or a Windows-to-Unix cross-compilation.

The initial public desktop run inspected during this review was
[40337156978](https://github.com/micha42-dot/uxzView/actions/runs/40337156978),
commit `c795e2ab26a114d1b477c7a1c0ad8e9d6ce09419`. All four compilation steps
succeeded there. Its Windows/Linux fixture failures and macOS installed-start
failure are documented in [CI_REPAIR_2026-10-26.md](CI_REPAIR_2026-10-26.md).
The prepared corrections have not yet run on those GitHub hosts.

## Build requirements

Previously, Exiv2, LibRaw or Qt Network could disappear from a runner without
failing CMake configuration. The application would then build with fewer
features. `IX_REQUIRE_FULL_FEATURES=ON` now rejects a missing metadata backend,
RAW preview module or extension manager. All desktop CI jobs and Windows release
preparation require these targets. Normal developer builds keep optional
dependencies optional; PowerShell exposes the check as `-RequireFullFeatures`.

Qt deployment now receives installed module paths relative to the install
prefix on every platform. macOS retains `Contents/PlugIns/IxView`; Linux uses
`lib/IxView`, and Windows uses the configured binary directory. This follows the
[Qt 6.8 deployment contract](https://doc.qt.io/qt-6.8/qt-deploy-runtime-dependencies.html)
and avoids using the corresponding build-tree modules as deployment inputs.
The custom Windows portable packager remains the release path.

Installed runtime checks now load both modules with the OS library loader and
resolve the RAW and extension-manager entry points. They do not invoke the
functions, open dialogs, connect to the network or start recognition tools.
This catches missing transitive libraries or missing exports that a PNG-only
smoke check would miss. `scripts/verify_native_modules.py` runs after native CI
has removed SDK library environment variables, and in Windows portable/release
preparation. Six offline cases cover missing files, unresolved dependencies,
missing symbols, the macOS layout and bundle-process failures; real loading is
checked locally on Windows.

On macOS the verifier invokes the installed application's `--probe-modules`
command. The loader then uses the bundle executable's dyld context. Qt's
[macdeployqt implementation](https://github.com/qt/qtbase/blob/v6.8.3/src/tools/macdeployqt/shared/shared.cpp)
can rewrite copied non-Qt libraries to `@executable_path/../Frameworks`;
loading those modules inside Python would incorrectly use Python's executable
directory. The diagnostic command exits before profile initialization and has
no effect on normal startup or lazy module loading. Windows/Linux checks use
the OS loader directly; the same application diagnostic is available there.

The five CTest suites now declare `RUN_SERIAL` and a 300-second timeout per
suite. Their GUI/profile state must not overlap even with `ctest -j`. Native
installation, packaging and installed-module logs are printed on failures and
uploaded alongside the existing compiler, startup and QtTest logs.

Building without Exiv2 also exposed an unused `encoded` parameter in its
metadata-disabled branch; the branch now explicitly marks it unused.

## Local evidence

The review starts from a fresh ZIP selected by the public-source policy and
checks its extracted files against the workspace before configuring. No old
CMake cache, generated source or object file is copied into these builds.
The installed project SDK is supplied explicitly through `CMAKE_PREFIX_PATH`.

Raw commands, return codes and timings are retained under
`build/portability-review-2026-10-27`, with the runner output in
`build/portability-review-run.log`. Reports distinguish deliberate configure
failures from unexpected build/test failures.

- Fresh reduced and full Windows builds succeed. The reduced variant disables
  Exiv2 and Qt Network and omits test targets; PNG and JPEG decoding succeed.
  Final variant builds have no compiler warnings.
- All three full-feature guards reject deliberately unavailable Exiv2,
  Qt Network and LibRaw dependencies at configure time. The RAW check is retained
  in `require-raw.log`; the other two appear in `summary.json`.
- The extracted full-source build passes all five CTest suites, including a
  repeated `ctest -j 5` request with verified serial/timeout properties. Of 270
  reported checks, 258 pass and 12 explicitly skip because the source ZIP does
  not include downloaded camera fixtures or prebuilt recognition packs.
- The fully equipped workspace passes all **270 Qt checks**, with no failures
  or skips, through `scripts/build.ps1 -Test -RequireFullFeatures`. The five-suite
  run takes 93.03 seconds. Source-side Python validation passes **41 tests**.
- The final application/module diagnostic succeeds in reduced, extracted-full
  and workspace builds. A copied application with both modules absent returns
  status 2 and reports both failures. A separate module-only test stage also
  fails through the real Windows loader when runtime DLL dependencies are absent.
- Both PowerShell entry points and all five Unix CI Bash blocks parse
  successfully. The generated Windows Qt deployment script names the installed
  `bin/ixrawpreview.dll` and `bin/ixextensions.dll` inputs.

Additional final reports: `final-summary.json`, `module-probe-summary.json`,
`source-probe-python-tests.log`, `source-final-ctest-parallel-request.log`, and
`workspace-full-build-test.log` in the review folder. The real missing-dependency
log is `build/portability-missing-module-dependency.log`. The application probe
does not invoke recognition engines or manager functions. These are Windows
results; mocked macOS process/layout checks are not native macOS execution.

After final redeployment, all **270 portable Qt checks** pass without SDK paths,
with no skips or failures. Real QR/OCR engines, all five OCR models, moved tool
storage and relocated-profile startup pass. Both installed modules and all
three exported entry points load successfully. Deployment retains 135 PE modules
with no unresolved imports and preserves the existing seven tool packages.
Reports: `build/portability-final-deploy.log`,
`build/portability-final-installed-modules.log` and
`build/portability-final-portable.log`. Windows/source archives are regenerated
after these checks; README and both existing README images remain unchanged.

## Remaining confirmation

This Windows host has no configured WSL distribution and no native macOS
compiler/runtime. A Windows build does not prove Linux or macOS execution.
Upload the prepared sources and require all four desktop jobs to pass through
configure, compilation, CTest, installation, installed PNG decoding and native
archive creation. Preserve the uploaded diagnostics if any step fails.

The macOS installed check uses the shipped Cocoa platform; Linux uses
XCB under Xvfb. SDK plugin/library environment variables are removed before both
checks. GitHub's [runner matrix](https://github.com/actions/runner-images)
still lists `macos-15` as arm64 and `macos-15-intel` as x86_64. Native desktop
behavior, Windows 10 execution and macOS signing/notarization remain separate
checks from buildability.

The later run `8878b46` confirms Linux compilation and all five CTest suites.
It exposes a test-only canonical temporary-path mismatch on Windows and both
Macs, plus a missing offscreen plugin in the installed Linux package. The
prepared path assertion and XCB/Xvfb corrections pass all 42 local Python tests
including eight simulated workflow cases; the native jobs must run again.
See [CI_REPAIR_2026-10-27.md](CI_REPAIR_2026-10-27.md) for logs and scope.
