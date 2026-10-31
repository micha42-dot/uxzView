# Navigation and modal lifetime review — 30 October 2026

This follow-up covers opening requests, optional save questions and navigation
that does not actually change the current file or page. It retains version 0.5.0.

## Reproduced failures

Eleven new workflow cases were run before changing product code. Nine failed;
two boundary cases already behaved correctly. Setup and cleanup add two passes
to the Qt report. Evidence: `build/navigation-review-before.txt`.

- An opening request borrowed its caller's QString across a modal question.
  Changing that caller value while the question was open redirected the request
  to a third file. Folder-list replacement could also invalidate a borrowed list
  entry. The regression uses a live caller value, avoiding deliberate invalid
  memory access while proving that the selected request was redirected.
- Folder navigation could reload the current image in a one-image folder,
  discarding edits/Undo or asking about saving without going anywhere.
- First/last-page commands could reload the current page and discard edits.
  Previous/next-page keyboard commands at a boundary could ask about saving
  before checking that no target page existed.

## Changes and cost

`openPath` owns its QString parameter across modal event processing. QString
sharing avoids an eager string-buffer copy. No pixel buffer or image-cache entry
is added.

File navigation, first/last/random file selection and page navigation skip a
target that is already displayed. Page bounds are checked before the save
question; the target is checked again after saving because export can change
the page count and current page. First/last-page commands share this path.

These no-op commands avoid starting load/display work and retain the existing
display image, edits and Undo. This is verified behavior, not a general image-load
benchmark. Explicit refresh/open requests and animation frames retain their load
path; a blanket same-path return inside `openPath` would break those uses.

## Verification

The new cases cover the aliased caller request, five file-navigation commands in
a one-image folder, four page-edge commands, optional questions on/off, retained
edited/display pixels and Undo, and unchanged source hashes. Logs use
`build/navigation-review-*`. The main and workflow suites retain real folder
navigation, animation, reload and save checks.

After correction, all eleven new cases pass (13 including setup/cleanup),
and all eight suites pass 445 Qt cases without skips. All 45 Python tests pass.
The native Windows run passes those eleven cases plus the save-revision check
(14 including setup/cleanup). Core evidence:
`build/navigation-review-validation-summary.json`; native evidence:
`build/navigation-review-native.txt`. The portable runtime has a separate
`build/navigation-review-portable.log`.
The SDK-independent portable runtime also passes all 445 cases, QR recognition,
all five OCR languages and relocated-profile startup.

The first full core run had one existing save-revision case fail with access
denied; the navigation cases passed. The unchanged save case then passed three
isolated runs (`navigation-review-save-recheck-*.txt`). Its original cause is
unproven; no save-backend, revision check or overwrite guard was changed. The
initial evidence is retained in `navigation-review-initial-workflow-failure.txt`
and `navigation-review-initial-validation-failure.json`.
The subsequent full core run and targeted native save check pass unchanged.

Native Windows 10, Linux and macOS execution remains open. The targeted Windows
checks do not establish filesystem locking or complete reentrancy safety for all
dialogs. Main-window orchestration remains a candidate for incremental extraction.
