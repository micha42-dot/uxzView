# Critical export and integration review — 2026-11-09

Version 0.5.4, Unreleased. This follow-up examines save-format/path agreement,
atomic publication, JPEG metadata boundaries, unusual pixel/color layouts,
optional Exiv2 builds, source/license inventories and deployed runtime isolation.
It is a targeted review with regression evidence, not proof that every platform
or image variant is correct.

## Fixed: oversized JPEG comments could silently lose text

`writeJpegli` previously truncated each UTF-8 comment to 65,533 bytes. This could
discard text and split a Korean character or emoji without warning. A reproduced
case overwrote an existing target successfully despite losing comment data.

The encoder now rejects an oversized comment with a translated message recommending
PNG or TIFF. The existing QSaveFile transaction discards staged output, preserving
the previous target. Comments at the exact marker limit remain accepted. Tests
cover that boundary with ASCII, Korean and emoji and verify that an oversized
comment leaves an existing destination byte-for-byte intact. No arbitrary text
splitting convention is introduced into JPEG metadata.

## Fixed: empty native metadata forced a complete export buffer

Exiv2 can return a valid metadata object with no EXIF, IPTC or XMP. Its mere
presence previously enabled full-file buffering and an Exiv2 rewrite. The registry
now requests rewriting only when actual native metadata exists. Empty containers
use the encoder's direct QSaveFile path, retaining its ICC profile, image text
and density. Nonempty metadata keep the existing rewriting and revision guards.

The regression uses an actual empty Exiv2 result and checks byte identity against
the direct encoder, including ICC and Unicode text. Builds without optional
Exiv2 explicitly skip this backend-specific test. A signedness warning in export
working-set accounting was also removed without changing the accepted bounds.

## Other boundary checks

- Native gray profiles combined with transparency produce white for fully
  transparent pixels; no defect was reproduced in that case.
- Date-backed grayscale color conversion matches Qt's independent conversion;
  the source profile remains intact.
- Save preferences, suffix agreement, explicit per-export overrides, unchanged
  JPEG saves, source/target revisions, batch output and cancellation retain their
  existing regression coverage.
- Native sources remain pinned; license notices and executable provenance are
  checked during Windows packaging. No dependency or runtime tool was added.

## Verification and limits

The corrections above are included in commit `d749123`. The fresh aggregate
review and current verification status are in [REVIEW_2026-11-10.md](REVIEW_2026-11-10.md).
No completed benchmark is claimed by this earlier report. Existing README photos, personal portable
profiles and installed recognition tools are preserved. No commit, push or virus
scan is part of this review.

Native Windows 10, Linux and macOS execution, manual native file chooser behavior,
broader real CMYK print profiles and GPU-specific fullscreen transitions remain
open. Passing synthetic/reference cases does not establish universal support.
