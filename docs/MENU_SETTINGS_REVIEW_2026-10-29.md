# Menu and settings layout review — 29 October 2026

## Layout decisions

Settings previously used 860×580 logical pixels, with a separate four-control
preview page and expanding numeric fields. The initial size is now 800×480,
bounded by available screen space. Category changes do not resize the window.
Preview settings move into a frameless section on Display, reducing ten categories
to nine. Stored keys and Cancel/OK semantics are unchanged.
The section uses a heading and spacing; it has no decorative border.

Numbers, theme/language/icon selectors, screenshot shortcuts and standalone
colour/cache buttons use content-sized widths. Long checkbox captions wrap at
word/character boundaries using Qt text layout, retaining their complete original
accessible names. This layout work happens only when opening Settings, without
image buffers, background polling or changes to image rendering.

The subsequent canvas adjustment removes the sunken Qt frame in window mode.
ImageView initializes without a frame; fullscreen transitions no longer change
that policy. The status bar retains its independent 1-pixel top separator, also
when neighbor previews are visible. Verification uses the existing fit/navigation
and fullscreen cases.

Colour correction uses a 700×540 initial window instead of 740×640; rotation
uses 620×540 instead of 620×640. Their existing scrollable bodies retain controls
on smaller displays. Resize retains its larger two-column layout because it has
substantially more controls. Library/format/result lists, OCR output, histogram
and image-information windows retain space useful for data and previews.

## Menu audit

| Menu | Scope reviewed |
| --- | --- |
| File | Open/recent files, library, slideshows, batch, file operations, export, printing and final Exit placement |
| Edit | Selection, crop, clipboard, Undo/Redo and image-dependent availability |
| Image | Geometry, colour, depth, histogram and corrections |
| View | Chrome toggles, fit choices, fullscreen, previews, navigation and zoom |
| Options | Preferences, animation, JPEG operations, screenshots and watched folders |
| Help | Format list, keyboard help and developer/assistance credits |

Six inherited captions are shortened in all five languages, including Image
library, Batch processing, Invert selection, Crop visible image area, Canvas size
and Screenshot. Command IDs, shortcuts and reference ordering remain intact.
Reference IDs describe the original command mapping; these captions are viewer
wording. Disabled selection-dependent operations remain disabled without a frame.

The classic batch entry is named for renaming but previously opened in conversion
mode. It now starts in rename mode and disables format/resize/loss controls that
do not apply there. Switching to conversion enables them without clearing values.
Existing native rename/identity tests also assert the initial mode.

## Verification

Ten new data-driven UI checks traverse every category in five languages and two
themes. They check stable compact size, category visibility, no horizontal page
scrolling, numeric field widths and reachable confirmation buttons. Display and
fullscreen captures are saved under `tests/output/ui-audit/settings-compact-*`.
Existing settings persistence/Cancel, immediate application, crop ratios,
recognition, editing and native DPI tests remain part of validation.

All eight suites pass 428 Qt cases without skips; all 45 Python tests pass.
The complete 33-block validator passes, including native Windows checks at
100/125/150/200 percent scaling. After removing the preview border, the complete
core checks and all 38 native UI quality cases at each of those four scales pass
again. The portable runtime is checked separately without SDK paths, including
QR, five OCR languages and relocated-profile startup.

The final border adjustment is recorded in
`build/menu-settings-frameless-validation-summary.json` and
`build/menu-settings-frameless-scale-*.txt`; the preceding full native matrix is
in `build/menu-settings-validation-summary.json`.

The later canvas-frame change passes all eight suites again and the three native
fit/navigation/fullscreen cases at all four scales. Its logs use
`build/window-border-*`; the restored-window capture is
`tests/output/fullscreen-restored-fit.png`. The status divider remains visible.

Logs use `build/menu-settings-*`. Windows automation and widget captures do not
establish native Windows 10/Linux/macOS appearance. Very small logical screens
can still require vertical scrolling; minimum readable text and usable controls
take precedence over forcing every page into an arbitrarily small fixed area.
