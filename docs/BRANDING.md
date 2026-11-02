# Branding assets

Michael Förtsch supplies the two transparent source logos:

| Effective app theme | Source | Runtime icon |
| --- | --- | --- |
| Light | `resources/logo-light.png` | `:/ixview-light.svg` |
| Dark | `resources/logo-dark.png` | `:/ixview-dark.svg` |

An explicit app theme takes precedence. With System selected, the logo follows
Qt's system color-scheme notifications. Existing windows inherit the application
icon. The About dialog updates its visible logo on a theme change, including
when it is already open. The two SVG icons are cached; paging images does not
reload them or change their cache keys.

## Desktop and packaged icons

The fixed launcher/file assets use the supplied grey variant so that the mark
remains visible on both light and dark backgrounds:

- Windows executable: `resources/uxzView.ico`.
- Linux desktop installation: `resources/ixview.svg`.
- macOS app bundle: `resources/uxzView.icns`.

Runtime theme switching and the fixed packaged launcher assets are separate.
The desktop file/shortcut does not switch between the two PNGs through the app's
appearance setting. This follows Qt's separate handling of
[window and packaged application icons](https://doc.qt.io/qt-6.8/appicon.html).

## Regeneration

```sh
python scripts/build_icons.py
```

Pillow is a development tool for this command. The generated assets are committed,
so ordinary CMake builds do not need Pillow. Both PNG sources are retained exactly
as supplied. The builder preserves alpha, centers the source on a transparent
square, and encodes equal-color pixel runs as SVG rectangles. ICO and ICNS sizes
use nearest-neighbor scaling to retain the pixel forms.

`appearance.cpp` owns the runtime icon. MainWindow inherits it; setting a fixed
per-window brand icon would prevent later theme updates. An unchanged effective
theme does not replace the cached application icon. About boxes are marked with
`ixBrandLogo`, so ordinary warning/error illustrations retain their own roles.

The UI regression test switches light/dark/system, checks existing windows and
an open About dialog, verifies transparent corners and retains the loaded image.
Pixel/alpha comparisons against both original sources and byte-identical
regeneration were also checked locally. Native Windows 10, Linux and macOS
visual verification remain separate from the local Windows 11 checks.

## Local verification

Windows 11: all 665 Qt cases in nine suites and all 46 Python tests pass without
failures or skips. The same Qt suites pass SDK-free in the portable runtime,
along with installed modules, OCR/QR, five OCR language models and relocated
profiles/tools. Native logo and title-bar checks pass at 100%, 125%, 150% and
200% scaling. Results: `build/review-brand-final-validation-summary.json` and
`build/brand-runtime-summary.json`.
