# Contributing to üxzView

Start with the [build guide](docs/PLATFORMS.md), [architecture map](docs/ARCHITECTURE.md) and [development workflow](docs/DEVELOPMENT.md). The latter covers tests, debugging, performance measurements and release checks.

Use C++20, Qt 6.8 or newer and the existing `.clang-format`. Keep platform-specific code behind the platform boundary. Keep optional modules lazy; document new dependencies, licenses and deployment costs. Prefer a change in the component that owns the behavior over another branch in `MainWindow`.

Use a branch and a PR describing the problem, resulting behavior and checks actually run. Add a regression for reproducible bugs. Prefer synthetic inputs; camera fixtures need a clear license and pinned hash. UI changes need an interactive desktop check as well as offscreen tests. Identify untested platforms.

Keep generated files in `tests/output`, local SDKs, private profiles and binary packages out of Git. Public sources/build recipes belong in the repository; release assets hold binary downloads.

Contributions are GPL-3.0-or-later. Do not add proprietary resources, private photographs or code of uncertain provenance. Describe üxzView as inspired by IrfanView and nomacs.
