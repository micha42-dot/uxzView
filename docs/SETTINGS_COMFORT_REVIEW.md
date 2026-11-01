# Settings interaction review

The Settings dialog keeps its vertical category list and stable window size,
with more room for operating controls. Previews now has its own page instead
of extending Display. Existing preference keys and OK/Cancel behavior remain.

## Changes

- At least 32 logical pixels for control and popup-row height; numeric fields
  start at 96 logical pixels wide. Font metrics can increase these dimensions.
- Dropdown arrows stay 8 × 5 logical pixels inside a wider clickable area.
- Checkboxes accept clicks across their row. Their labels wrap for the current
  width without inserting newlines into the stored or accessible label.
  Wrapping is cached by text, font and width, rather than rebuilt every paint.
- Categories have comfortable row heights and measured text widths, including
  the longer French storage label. The page can scroll vertically on small
  screens; it does not grow the whole dialog when switching categories.
- Unfocused numeric fields and dropdowns ignore wheel changes. Keyboard focus
  enables the usual wheel behavior. Newly added custom crop ratios follow the
  same sizing and focus rules.
- Disabled preview fields and the disk-backing budget also disable their form
  labels, while retaining their saved values.

The helpers use native Qt widget behavior and the existing palette. No image
path, decoder, background job or startup service was added.

## Verification

The development build passes 608 Qt cases across nine suites, with no failures
or skips, and all 46 Python tests. The same Qt cases also pass SDK-free in the
portable runtime, including the installed OCR/QR engines and five OCR models.
Native Windows 11 checks pass at 100%, 125%, 150% and 200% scaling: both themes,
all five languages, the three interaction regressions, settings persistence and
cancellation, custom crop ratios, fullscreen information and portable-mode
settings. Screenshots were inspected for compact arrow glyphs and readable
controls. These are targeted Settings checks, not a rerun of every native test.

Validation results are recorded in `build/review-settings-final-validation-summary.json`
and `build/settings-runtime-summary.json`. The interaction regressions cover
full-row clicks, Space activation, narrow-window wrapping, focus-dependent
wheel input, dependent labels and dynamically added crop rows. The existing
settings tests cover apply, cancellation, persistence and portable-profile
handling. Layout checks exercise both themes and all five UI languages.

Native Windows 10, Linux and macOS presentation still needs checking on those
systems. Logical target sizes and passing layout checks do not establish how
comfortable the controls are for every user or input device.
