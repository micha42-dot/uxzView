# Memory and efficiency follow-up — 29 October 2026

## Confirmed unnecessary source ownership

BatchPipeline::run copied the decoded ImageDocument into transform(), leaving
the reader result owning the original pixels until after output publication.
Resizing produced a new buffer, but could not release the old one. The preview
path had the same extra owner and also retained its transformed output while
decoding the encoded preview.

The pipeline now moves the document into transform(). Preview generation drops
the transformed document after saving, before decoding the saved preview.
The source remains alive while a transform needs it; outputs, metadata, guards
and original input files retain their existing behavior.

A custom codec supplies a 1024×768 RGB32 buffer with a cleanup callback. The
test resizes to 8×6, checks whether cleanup occurs before the output exists,
and checks exact output dimensions/colour. It failed against the preceding
implementation and passes with ownership transfer. Evidence:
`build/memory-pass-before.txt` and the maintenance-suite reports. This proves
the lifetime of that 3 MiB fixture, rather than a measured process-RAM reduction.
A 6000×4000 RGB32 source contains 96 MB of pixel data; the avoided ownership
can matter substantially with several workers. Peak RAM still includes
overlapping transform buffers, encoders, metadata and allocator overhead.

## Cache invalidation work

Selective eviction formerly searched the complete changed-path list for every
cached document and thumbnail: O(changed paths × cache entries) comparisons.
It now prepares a hash set before acquiring the cache mutex and uses expected
O(changed paths + cache entries) lookup work, excluding string-length costs.
Windows paths are case-folded; Unix paths remain case-sensitive. LRU order,
preview/APNG invalidation and stale in-flight result rejection stay intact.
The lookup set uses additional temporary O(changed paths) storage; it does not
raise the persistent pixel-cache budgets.

The existing semantic test now includes 10,000 unrelated paths, duplicates,
empty paths, Windows case variation and a retained neighbor. This validates
eviction correctness at that input size; no wall-clock speedup is claimed.

## Remaining limits

Batch parallelism is still a configured 1–4 workers, not a process-wide memory
budget. Intermediate transform/encoding buffers can overlap. Pixel history
intentionally retains one oversized nearest inverse operation. These are not
hard RAM caps. Memory-aware scheduling and coalesced JSON-catalog updates remain
larger follow-ups; native Windows 10/Linux/macOS execution remains outstanding.

Validation logs for this pass use `build/memory-pass-*`; the preceding complete
native UI/DPI run is `build/0.5.0-followup-validation-summary.json`. This pass
changes core buffer ownership and cache lookup, without changing UI code.
