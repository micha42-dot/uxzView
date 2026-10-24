# Function extensions

The Windows package includes two offline tools in `tools` beside the application:

- **QR / barcode reading:** ZXing-C++ 3.0.0. Recognizes codes in the current image or selection, displays their format and text, and lets you copy or save the results. Links are never opened automatically.
- **Text recognition:** Tesseract 5.5.3 with Leptonica 1.85.0. Select one or more installed OCR languages. English, German, French, Japanese and Korean fast models are included and independently removable.

Use **Image → Function extensions → Read QR / barcodes** or **Recognize text**. If the engine or an OCR language is missing or disabled, invoking recognition opens the function-extension tab with the required package selected. Cancel leaves the image unchanged. The recognition dialog also has an **Extensions** button for managing installed packs; language choices are refreshed on return. Copy/save actions become available when a result exists.

Alternatively, draw a rectangle with the left mouse button, then right-click inside it and choose either command. A stationary right-click opens this menu in window mode; right-drag still moves the selection or pans the image. Fullscreen left/right clicks retain previous/next navigation; the keyboard context-menu key (or Shift+F10 where supplied by the platform) opens recognition without changing images.

The tools read the currently displayed image pixels, including edits. A rectangular selection limits the input; otherwise the complete image is used. Transparent pixels are composited onto white for recognition. Results are plain text. Recognition does not modify the document or its undo history. Each invocation starts a separate native process; no Python installation, cloud API or background service is required.

## Installation and storage

Open **Options → Manage extensions → Function extensions**. Select a package and choose **Install / update**, or use **Install local package** with an `.ixtool` file. Both engines and all five languages are ready to use in a fresh Windows package. Reinstall Tesseract and at least one language if they were removed. Tools can be enabled or disabled immediately. Select an installed function extension and choose **Remove** to delete it and all retained versions. OCR languages can be removed separately; removing Tesseract leaves separately installed language packs available for later reuse. Active recognition jobs temporarily prevent removal of their engine and selected languages, including across viewer instances. The downloaded `.ixtool` installation archive is retained, so it can be reused. Image-format packages remain in their own tab and require a restart.

The bundled catalog contains pinned metadata and measured package/installed sizes. There is currently no public download server configured. In a local build, the manager finds `dist/tools`; with a deployed Windows folder, place `tools` beside `uxzView`. When a pack is absent, the Install button opens a file picker for that exact catalog entry. A supplied HTTPS catalog can provide downloads; sizes, package SHA-256 and every contained file are verified before activation. Catalogs are loaded only on request. There are no background updates.

The default tool folder is `<application folder>/tools`, including in normal profile mode. The function-extension tab shows this path and lets you choose an empty replacement folder. Image-format packages retain their separate profile folder. A cancellable copy retains originals and switches the selected folder after restart; installation and activation for that section remain disabled until then. Nested folders, links, occupied destinations and concurrently installed packages are rejected. Previously selected custom stores and their legacy layout remain usable. An unbundled source build also recognizes an existing profile-based tool store.

Tools already beside the application move with it and are not copied into `IxView-data` when enabling portable mode. A first portable import snapshots external custom tools into `IxView-data/tools` and remaps the setting; legacy shared stores remain compatible. A custom external folder selected after enabling portable mode stays external. Move that folder separately or select a folder beside the application before moving it.

## Building the packs

The CMake application build does not download recognition engines or models. Build the packs before Windows deployment:

```sh
python scripts/build_tool_packs.py --jobs 4
```

Use `--only qr` or `--only ocr` to build one group. Python 3.14+, CMake, Ninja and C/C++ compilers are required for building. Windows uses the project's locked UCRT SDK (`python scripts/bootstrap_sdk.py` first); Linux and macOS use their native compilers. Windows x64 has been exercised locally. Native Linux/macOS tools still need validation; those builds rely on the native system C/C++ runtime.

The builder verifies immutable archives and model files against `scripts/tool-sources-lock.json`, builds static recognition engines, and writes:

- `dist/tools/*.ixtool`: separate engine and language packs.
- `dist/tools/uxzView-tools.json`: catalog with exact bytes and SHA-256.
- `resources/tool-catalog.json`: metadata only, embedded by the next viewer build.

After building packs, rebuild the application so its catalog contains the current hashes. The Windows packs include the three required compiler runtime DLLs and their licenses/source origins; each engine pack is self-contained. Windows deployment unpacks both engines and all five models directly into `uxzView/tools`; the Windows ZIP includes these ready-to-use files, without duplicating the `.ixtool` archives. Linux/macOS source builds can install their native packs through the manager. Keep the optional packs beside the catalog when hosting it; relative package URLs resolve against that catalog. Publish the corresponding source ZIP and linked third-party source archives alongside binary downloads.

## Package contract

`.ixtool` is a bounded streaming container, not a ZIP executable installer:

1. Nine-byte magic `UXZTOOL1\n`.
2. Big-endian 32-bit JSON-header length, maximum 1 MiB.
3. UTF-8 manifest with `schema`, `id`, `version`, `kind`, platform/architecture, protocol/entry point or language, and an ordered `files` array.
4. Uncompressed file payloads in that order. Each entry specifies a safe relative `path`, positive `bytes`, `sha256` and optional `executable` flag.

Packages are limited to 512 MiB and 256 files, installed through a staging directory and atomic state update. Interrupted or invalid installs leave the previous active version intact. New packages live under `<tool folder>/<id>/<version>`; the application default is `uxzView/tools/<id>/<version>`. Existing nested stores are read without relocation; `tools.json` identifies the active versions. Updates retain older versions and preserve a disabled state. Removal publishes the state change atomically and deletes the validated package tree; state-write failures restore the directory. Linked package trees are rejected. Per-job leases pin engine and language versions until work finishes or is cancelled, allow concurrent recognition, and recover locks from terminated processes. Leases are never copied when changing profile or extension folders. Paths are relative so packs survive folder relocation.

Deployment preserves the existing `uxzView/tools` directory, including disabled tools and removals. Release packaging uses an independently verified snapshot under `build/bundled-tools`, rather than copying personal state or third-party packs from that installed directory. Each release therefore starts with both tools enabled, without publishing local preferences. `bundled-tools.json` inventories the shipped files; source origins and licenses accompany the engines/models. The viewer retains its 160 MB size budget, with a separate 25 MB budget for the included recognition files.

Supported protocols are deliberately narrow: `zxing-json-v1` and `tesseract-text-v1`. The viewer exports a private PPM file in a temporary job folder, prepares selected model files, and starts the executable without a shell. Work is cancellable, time-limited (30 seconds for codes, 120 seconds for OCR) and output-limited (4 MiB). Temporary files are removed when the dialog closes. Inputs are limited to 100 million pixels and 32,768 pixels per dimension; select a smaller area for larger images.

Hashes verify integrity, not publisher identity. Native tools run with the user's permissions and must come from a trusted publisher; there is no package-signature infrastructure.

## Verification and limits

`tests/extensions_ixview.cpp` covers package integrity, traversal/OS/catalog rejection, cancellation, activation, corrupt state preservation, folder migration, portable custom-folder import, manager installation/deactivation/removal, actual QR/Code 39/blank images, selection → right-click → recognition, active-use removal protection, cancellation/reinstallation and actual English OCR with all five models installed. Run `ctest --test-dir build --output-on-failure`; set `IX_REQUIRE_TOOLS=1` to fail if optional artifacts are missing. Otherwise real-tool checks explicitly skip when packs have not been built.

These fixtures establish integration, not broad OCR/barcode accuracy. Handwriting, difficult photographs, every barcode family and Japanese/Korean recognition quality need further real-world testing. Background removal, vision.cpp and ONNX tools are not included.
