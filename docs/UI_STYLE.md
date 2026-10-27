# Interface style

The image is the main content. Keep controls quiet, recognizable and predictable;
use layout instead of explanatory labels. This guide describes the current
desktop chrome.

## Appearance and layout

Settings → Interface stores `ui/theme` as `system` (default), `light` or `dark`.
Apply it through `applyInterfaceTheme()` on the GUI thread after AppStorage has
selected the profile and before constructing the first window. The same entry
point handles accepted preferences. Cancel does not apply or persist a change.
The System mode follows Qt's platform light/dark hint; unsupported/unknown hints
use the startup palette's brightness. Explicit modes supply the application
palette even when a platform cannot force its native appearance.

`appearance.cpp` owns a single application-lifetime controller and a shared
Fusion widget style. A platform scheme signal updates System mode; there are no
polling timers, optional libraries or per-image theme checks. Palette roles include
disabled text, selections, tooltips and accents. Keep this API compatible with the
Qt 6.8 minimum (`QStyleHints::setColorScheme` is available there; see the
[Qt documentation](https://doc.qt.io/qt-6/qstylehints.html#colorScheme-prop)).
Native file pickers and OS-owned title bars remain platform controlled.

The menu strip and toolbar use the same Window background. Explicitly refreshed
toolbar/status/preview palettes must follow changes too. Preserve the separately
configured image and fullscreen backgrounds (black by default). Interface-only
settings must not rescan the folder, decode pixels, rebuild the display image,
clear selections or reset zoom. MainWindow compares fit/sort/color-processing
preferences before starting the corresponding work.

Use `dialog_layout.h`: 16 logical pixels outside, 12 between sections, 16 between
form columns and 8 between form rows. Use layouts instead of fixed coordinates.
Resize, color and rotation dialogs place their content in a scroll area and keep
OK/Cancel in a separate footer. Preserve object names, field ranges, model wiring
and language-specific fonts. Explanations of gestures belong in tooltips.

## Live language

Settings → Interface applies the language on OK; Cancel leaves it unchanged.
German, English, Korean, Japanese and French use the same persistent actions and
widgets. Keep command IDs, shortcuts, user queries, paths and image state intact.
The language change updates fonts and widget locales, including decimal separators,
without changing the process-wide locale or directory order.

For persistent UI text, use `bindUiText(object, property, source)` from
`interface_language.h`. Retain the untranslated source and optional formatting
arguments; do not attempt to reverse an existing translation. MainWindow handles
LanguageChange with `retranslateUiText`, status and action-state refreshes. Use
`bindToolbarDescription` for tooltip/status/accessibility text that includes native
shortcut labels. Ephemeral dialogs read `IX_TEXT` on construction and inherit the
explicit interface locale through `dialogLayout`.

The controller owns the current Qt translator for its application lifetime.
Embedded application catalogs become immutable after first load, allowing worker
readers during switches. No window/action reconstruction, timer, image conversion
or folder rescan belongs to the language-change path. Static bindings and working
menu sources are checked by `tests/test_localization.py`; channel-order notation
such as RGB → BGR remains invariant.

## Icons

Original vectors live in `src/ui/icons.cpp` on a 24-unit grid. Rounded strokes
are **1.25 units**, down from 1.65, with roughly 18-unit silhouettes. Keep the
same recognizable shapes: magnifying glasses with plus/minus for zoom, chevrons
for neighboring images, page outlines for pages, and sliders for settings.
Icon keys are stable command wiring; changing artwork must not change commands,
shortcuts, accessible names or tooltips.

Use the active/disabled desktop `WindowText` colors. Avoid making symbols look
lighter by reducing their contrast. The current raster cache revision is `v5`;
bump it for changed artwork/geometry. Palette, logical size and device ratio
remain part of the cache key. The image placeholder stays centered and capped
at 32 logical pixels.

Toolbar sizes remain 20, 24, 28 and 32 pixels. Thin strokes do not reduce button
padding or clickable areas. `outlineIconsAndZoomSemantics` checks all four sizes
at 100/125/150/200 percent scaling, plus palette/mode and zoom semantics.

## Toolbar and fields

The toolbar uses the window palette, subtle separators and quiet hover/pressed
states. Keyboard focus keeps the palette's highlight color. The command search
remains optional and right-aligned.

Zoom and search fields share the palette's Base/Text colors with light outlines
and small corner radii. `ZoomComboBox` in `main_window.cpp` retains native combo
editing, popup, keyboard and accessibility behavior; only its arrow painting is
custom. The rounded 1.25-pixel chevron follows the style's dropdown rectangle
and enabled/disabled text palette. Do not remove the visible dropdown affordance
when styling the field, or depend on fixed-color arrow assets.

## Status information

Status labels form one continuous row, without sunken label or item frames.
Their information, stretch proportions and resizing grip remain available.
Colors continue to follow the application palette. Avoid replacing informational
values with extra captions, badges or decorative boxes.

## Review workflow

`liveLanguageChangePreservesDocument` switches all five languages through the
real Settings dialog in one edited document, both windowed and fullscreen. It
checks action identity/shortcuts, persistent search rows/query, translated menu
and accessibility text, formatted text, numeric locale, selection, scroll/zoom,
source revision, modified state and Undo. It also checks Cancel/invalid codes and
that directory indexing does not restart. `languageCatalogSupportsConcurrentReaders`
switches language while two workers repeatedly read translations. The native
appearance/DPI group repeats the windowed live-change case at all four scales.

Run `interfaceThemePreservesImageAndNavigationState`, `themedDialogsRemainUsable`
and `colorLayoutKeepsAdjustmentSemantics` in `ixview_workflow_tests`. The first
uses the real Settings dialog on a loaded document, compares rendered menu/toolbar
backgrounds, checks status/preview palettes and retains image/navigation state.
The second checks footer reachability in constrained-height dialogs in both
modes and all five languages. Review the `interface-{light,dark}.png`,
`settingsDialog-{light,dark}.png`, and resize/color/rotation captures under
`tests/output`. The full Windows validator also repeats appearance/navigation
and German/Korean/French dialogs at 100/125/150/200 percent scaling.

Run `outlineIconsAndZoomSemantics`, `optionalWidgetsAndNeighborNavigation`,
`keyboardZoomAndEditingFocus` and `renderUi` in `ixview_tests`. Inspect
`tests/output/icon-set.png`, `toolbar-light.png`, `toolbar-dark.png` and
`main-window.png`; rendered artifacts are local review evidence, not shipped
application resources. Check native keyboard/focus behavior and multiple DPI
settings before packaging. Keep before/after evidence under `build`.

Further design work should favor an optional compact/customizable toolbar.
Preserve image area, language-specific font policy,
contrast and accessible focus rather than adding decoration or helper text.

## Validation — 27 October 2026

The refinement passes all 20 local validator steps: 42 Python tests and 300 Qt
checks, no failures/skips, plus native window/file/tool workflows and
100/125/150/200 percent scaling. Direct review includes the 20/24 px symbols
in light/dark palettes, toolbar captures and the main-window/status layout.
The focused icon/zoom/toolbar/render run passes six checks including setup and
cleanup. Original command keys, search placement and icon-button hit areas remain
unchanged. This is Windows evidence; native Linux/macOS confirmation still
belongs to the desktop CI run.

Evidence: `build/design-refinement-tests.txt`,
`design-refinement-validation.log`, `design-refinement-validation-summary.json`
and `design-refinement-*test-results.*`. Before captures are retained as
`build/design-icons-before.png`, `design-toolbar-before-{light,dark}.png` and
`design-window-before.png`. A separate small-size review sheet is retained in
`tests/output/icon-set-small.png`.

The deployed portable runtime repeats all 300 Qt checks without SDK paths,
including required RAW fixtures, real OCR/QR engines, all five OCR models and
relocated tool/profile storage. Both native modules and their exports load.
Reports: `build/design-refinement-deploy.log`,
`design-refinement-installed-modules.log` and `design-refinement-portable.log`.
The Windows/source archives include the refined UI and guide; README and both
existing README images remain unchanged.

## Theme and dialog validation — 27 October 2026

The subsequent appearance/layout change passes **24 local validator steps**,
**42 Python tests** and **312 Qt checks**, with no failures/skips (159 main,
81 workflow, 43 quality, 12 storage, 17 extension). Native workflow checks
include both themes, all five languages and footer reachability at constrained
heights. Appearance/navigation and German/Korean/French cases also pass at
100/125/150/200 percent scaling. A behavior regression checks color-control
limits, defaults and white balance after replacing fixed coordinates.

All **312 portable Qt checks** pass against the deployed DLLs without SDK
paths; required RAW fixtures, real QR/OCR engines, five OCR models and relocated
tool/profile startup pass too. Both optional modules and their exports resolve;
deployment reports 135 PE modules with all imports resolved and preserves the
seven installed tool packs. Performance scope and measured light/dark values
are recorded in [PERFORMANCE.md](PERFORMANCE.md#interface-appearance--27-october-2026).

Evidence: `build/interface-validation-summary.json`, `interface-validation.log`,
`interface-*-test-results.*`, `interface-native.txt`, `interface-dpi-*.txt`,
`native-appearance-scale-*.txt`, `interface-deploy.log`,
`interface-installed-modules.log` and `interface-portable.log`. Inspect the
latest `tests/output` captures as described above. Windows build logs have no
compiler warnings. Native Linux/macOS/Windows 10 confirmation of this UI change
remains pending; the implementation uses the project's existing Qt 6.8 APIs.

## Live language validation — 27 October 2026

All **24 local validator steps**, **43 Python tests** and **319 Qt checks** pass,
with no failures/skips (159 main, 88 workflow, 43 quality, 12 storage, 17 extension).
The real Settings flow switches all five languages on one edited document in
window/fullscreen modes. It retains pixels, source state, selection, scroll, manual
zoom, Undo, action identity/shortcuts and search rows/query; Cancel and invalid
codes keep the previous language. Two workers read catalogs during repeated
switches. Numeric-editor and translated/formatted/accessibility text checks pass.
The windowed flow repeats natively at 100/125/150/200 percent DPI.

Visual review also caught the untranslated Image menu and malformed German
negative-channel labels. Working menu labels now have a catalog/encoding check;
static text bindings are included in translation-source validation. Reference-only
unimplemented entries remain outside this working-menu rule. Image-menu mnemonics
follow the selected language; command shortcuts retain their existing sequences.

All **319 portable Qt checks** repeat successfully without SDK paths, with required
RAW fixtures, real OCR/QR engines, five OCR models and relocated tool/profile
startup. Both optional modules and exports load. No compiler warnings are recorded.
This is Windows 11 evidence; native Windows 10/Linux/macOS confirmation of the
live switch remains pending. The implementation retains the Qt 6.8 minimum.

Evidence: `build/live-language-validation-summary.json`, `live-language-validation.log`,
`live-language-*-test-results.*`, `live-language-native-workflow.*`,
`live-language-native-scale-*.txt`, `live-language-build.log`, `live-language-deploy.log`,
`live-language-installed-modules.log` and `live-language-portable.log`.
Rendered captures: `tests/output/live-language-{de,en,ko,ja,fr}.png`.
The canonical Windows/source archives are rebuilt after validation; README and
both existing README images remain unchanged.
