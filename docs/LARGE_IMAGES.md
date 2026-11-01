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
buffers, weights and cache. 16-bit output uses the same bounded row pipeline with RGBA64 destination pixels.
Sharpening still requires separate RAM working buffers. Pixel doubling keeps its native nearest-neighbor path.

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

## Phase 2: file-backed original pixels

Performance settings offer `performance/swapEnabled` (default true) and
`performance/swapLimitMiB` (default 65536, 64 GiB; zero disables disk allocations).
They apply to future allocations immediately. Disabling or reducing the budget
does not invalidate existing images. The limit is aggregate per process,
including active documents, results, Undo and display-color copies.

`core/image_storage.*` allocates ordinary QImages while the estimated resident
working set fits RAM. Larger resize destinations use a mutable file mapping.
A bounded horizontal-row cache and vertical accumulation avoid full source
conversions and full-frame intermediates for both 8- and 16-bit filtering.
Nearest-neighbor retains its native source format. Resize maxima include the
remaining disk budget; disk capacity is checked at allocation, not while typing.

Private files live in `AppStorage::cacheRoot()/swap`, with a 64 KiB geometry,
format, stride and byte-count header. Physical disk space is reserved before
mapping: Windows FileAllocationInfo, Linux posix_fallocate, macOS F_PREALLOCATE.
Unsupported reservation, insufficient disk, address-space failure or exhausted
budget returns an error; it does not fall back to a sparse mapping that could
fault when the disk fills. The original file is never the backing store. On Unix, the directory and files
are owner-only (0700/0600), independent of the inherited umask. The creation gate
is released before physical reservation; live per-file locks protect the buffer
without serializing other processes behind multi-GB filesystem work.

QImage implicit sharing keeps the mapping alive through pixel Undo and display;
the final reference unmaps and removes it, including worker cancellation.
Registry state survives static destruction. Startup cleanup runs in a worker,
under a creation gate. Only generated filenames with recognized, consistent
headers and obtainable per-file process locks are removed. Live instances,
unknown files and links are preserved. This is temporary storage, not crash
recovery: unsaved edits are still discarded by default.

## Practical Phase 3: bounded IO and honest limits

PNG and sequential JPEG can decode directly into a unique, correctly typed
mapped QImage using Qt's existing libpng/libjpeg scanline handlers. Tests verify
the destination pointer remains mapped; a handler that replaces it is rejected.
EXIF orientations are applied into a second allocated image by pixel copying.
Progressive JPEG needs whole-image coefficient arrays, so images requiring the
mapped path are rejected rather than promising bounded RAM. Other oversized
codecs retain their ordinary guarded path and return errors above its limit.

JPEG thumbnails use decoder scaled-size reading even when the original exceeds
the normal allocation budget. A PNG thumbnail that would require a huge fresh
decode is omitted. The viewport retains original-pixel geometry and paints a
bounded visible rectangle; display caches never enter the document or export.
ICC display conversion of mapped originals is row-based and may need another
mapping. This additional backing counts against the same budget. Display
sharpening and non-streaming edits still need full RAM working buffers and can
fail explicitly; first reduce image size when using those operations.

Large PNG/JPEG exports stream original pixels into QSaveFile. Target revision
checks and beforeCommit cancellation still guard publication. PNG supports the
application's checked geometry; JPEG is limited to **65,500 pixels per axis**.
Other oversized output codecs are rejected before writing. Alpha flattening is
row-based, independent of the image's device pixel ratio. Full encoded EXIF/IPTC/
XMP rewriting is omitted for mapped or RAM-expensive output, with the existing
metadata-loss confirmation contract; ICC/DPI/text follow the native encoder.
This avoids building an entire encoded file in QBuffer. Small exports retain
normal metadata rewriting. Global Qt/plugin/environment limits can still differ.

## Example: 16 GiB physical RAM

Automatic RAM budget is 8 GiB. A native 32-bit 100,000 × 100,000 target needs
40,000,000,000 bytes (**37.25 GiB**) plus a 64 KiB backing header. It exceeds RAM
and fits the default 64 GiB disk budget only if enough budget and real free disk
space remain. A 16-bit RGBA target needs 74.51 GiB and exceeds that default.
Another retained 37.25 GiB original/display/Undo mapping cannot fit beside the
first in 64 GiB. Existing source pixels, row buffers, filters, plugin internals
and concurrent work add costs. File-backed pages are managed by the OS; neither
RAM budget nor disk budget promises a strict total RSS ceiling. Extreme sizes
are slow and can fail due to address space, filesystem or format constraints.

The 16 GiB example is an accounting example, not a claim that a 40 GB fixture
was processed locally. Regression tests force real mappings on small fixtures,
compare every resampling filter and 8-/16-bit results against the independent
legacy implementation, exercise cancellation and budget failures, and verify
normal/crash cleanup, active-instance locks, atomic output and original-pixel
Undo. A 100,001-pixel-wide mapped image checks fitted viewport painting.

Primary implementation references:
[Qt image allocation](https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/image/qimageiohandler.cpp?h=6.8),
[Qt PNG IO](https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/image/qpnghandler.cpp?h=6.8),
[Qt JPEG IO](https://code.qt.io/cgit/qt/qtbase.git/tree/src/gui/image/qjpeghandler.cpp?h=6.8),
[Windows allocation](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setfileinformationbyhandle),
[Linux reservation](https://man7.org/linux/man-pages/man3/posix_fallocate.3.html),
[macOS allocation](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/fcntl.2.html).
