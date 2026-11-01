# Large images

## Phase 1: RAM and resizing

Image-buffer operations use a process-wide budget selected at startup. Automatic
mode uses half of physical RAM, rounded down to whole MiB, with a 512 MiB floor
and 8 GiB ceiling. `performance/memoryLimitMiB=0` selects automatic mode; a positive
value overrides it. A 32-bit process is capped at 1 GiB. Windows uses
GlobalMemoryStatusEx, Linux sysinfo and macOS sysctl. An unsuccessful probe falls
back to 512 MiB. `--memory-info` reports the active budget and geometry ceiling.

The setting is under Performance and takes effect after restarting. Qt 6.8 uses
a non-atomic global image-reader allocation limit, so it is set before workers
start, rather than changed during concurrent decoding. An explicit
QT_IMAGEIO_MAXALLOC environment variable retains Qt's documented precedence.

The geometry ceiling is 1,000,000 pixels per axis, subject to the byte budget and
each decoder/encoder's own limits. This is not permission to allocate a million
square pixels regardless of RAM. Arithmetic is checked before allocating.

For 8-bit resampling, source conversion and filtering use individual RGBA64 rows
and a bounded horizontal-row cache. Intermediate rounding remains identical to
the previous two-pass implementation. The destination uses its final native
32-bit format. Its main output cost is four bytes/pixel, plus the source, row
buffers, weights and cache. Sharpening and higher-depth paths have separate,
larger working sets. Pixel doubling keeps its native nearest-neighbor path.

The resize dialog shows a live maximum at the selected aspect ratio and disables
OK above the accepted operation budget. The estimate includes filter work,
sharpening and input depth; errors report dimensions and the active MiB budget.
Undo, document/thumbnail caches, plugins and concurrent jobs have their own costs:
this is an operation budget, not a promise to cap total process RSS.

Cache/history accounting saturates at a separate positive signed-int bound;
raising RAM does not overflow QCache costs. PDF receives the caller's budget
explicitly across its module boundary, rather than owning a second global policy.

Verification: 560 Qt cases across eight suites, plus 46 Python tests. Tests pin a
512 MiB process budget for repeatable legacy boundaries. Additional cases cover
auto/manual/32-bit calculations, whole-MiB alignment, geometry and invalid BPP,
all six resampling filters across seven 8-bit formats, up/down/one-axis sizes,
gamma/fast modes, cancellation and the estimated acceptance boundary. Independent
legacy resampling is retained in tests/resample_reference.h. Real isolated
starts confirm automatic and 1024 MiB overrides. Evidence:
build/review-memory-phase1-final-validation-summary.json and
build/memory-phase1-startup.json.

## Pending storage work

Phase 1 still allocates its destination in RAM. File-backed images, stale-session
cleanup and bounded large-format IO belong to the subsequent storage phase.
Display previews must never replace original pixels for editing or export.
