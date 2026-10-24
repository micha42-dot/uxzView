![üxz logo](https://raw.githubusercontent.com/micha42-dot/uxzView/refs/heads/main/%C3%BCgz.png)

# üxzView

üxzView is an image viewer for Windows 10/11, Linux and macOS, built with C++20 and Qt 6. Inspired by IrfanView and nomacs.

**Currently in development.** Prebuilt packages currently target Windows x64 only. You can also build üxzView from source for Linux and macOS; native builds on those platforms still need validation.

## Features

- Fast folder browsing, keyboard navigation and image caching.
- Fullscreen viewing with mouse navigation, slideshows and zoom into a selected area.
- Optional fullscreen information in any corner: resolution, DPI, file size and selected metadata.
- Optional, centered thumbnail strip with 1–5 previous and next images.
- Search for editing commands directly from the toolbar.
- Resize, crop with preset or custom aspect ratios, rotate, flip, adjust colors and automatic correction.
- Visual batch processing with reusable profiles, watermarks and real export previews.
- Ratings, color labels, metadata search and duplicate detection.
- Internal trash, file-operation history and undo.
- Configurable screenshots and keyboard shortcuts.
- JPEG, PNG, WebP, TIFF, GIF, APNG, PSD composite images, SVG, RAW and additional plugin-provided formats.
- Multi-page images, animation playback, 16-bit image data and ICC/EXIF/IPTC/XMP support where supported by the format.
- Offline QR/barcode reading and text recognition from the image or a right-clicked selection, with removable tools and OCR languages.
- Separate image-format and function extensions, with configurable storage folders.
- Optional portable mode for settings, profiles, ratings, extensions and file history beside the app.
- German, English, Korean, Japanese and French interface options.

Format support, test coverage and remaining limitations: [project status](PROJECT_STATUS.md), [format compatibility](docs/FORMAT_COMPATIBILITY.md) and [quality checks](docs/QUALITY.md).

Program files and downloads use the ASCII name `uxzView`. Existing settings and portable profiles retain their `IxView` storage identifiers for compatibility.

## Portable mode

Portable mode is available under **Options → Settings → Storage locations** and takes effect after restarting. Existing user data is copied on first activation and retained in its original location. Move `portable.mode`, `IxView-data` and `tools` together with the app. [Details](docs/PORTABLE.md).

## Build from source

Requirements: a C++20 compiler, CMake 3.24+, Qt 6.8+ and libjpeg-turbo. The Windows SDK bootstrap installs the project's pinned dependencies locally.

### Windows

With Python 3.14+ installed, run from the project folder:

```powershell
python scripts/bootstrap_sdk.py
python scripts/build_tool_packs.py
./scripts/build.ps1 -Test
python scripts/deploy.py
```

Deployment also builds the smaller HEIF/AVIF libraries from pinned sources. Their source archives are downloaded once and cached locally.

### Linux and macOS

Install Qt and the dependencies listed in the [platform build guide](docs/PLATFORMS.md), then run:

```sh
bash scripts/build.sh -DCMAKE_PREFIX_PATH=/path/to/Qt
```

The guide includes commands for Ubuntu and macOS, optional dependencies and native installation. The shared source code and build workflows are prepared for all three systems; successful native Linux/macOS runs are still outstanding.

## Recognition tools

The Windows package includes QR/barcode reading (ZXing-C++) and text recognition (Tesseract), with English, German, French, Japanese and Korean OCR. Tools live in `tools` beside the application and run only when invoked. You can remove individual tools/languages or change their folder under **Options → Manage extensions → Function extensions**. They work on the whole image or a selection and leave image pixels unchanged. Reinstallation and source builds: [function extensions](docs/TOOL_EXTENSIONS.md).

## Use üxzView as your default image viewer

### Windows 10/11

Run **`register-windows.bat`** in the portable application folder. It finds `uxzView.exe` beside it automatically. If you keep the script elsewhere, edit the `IXVIEW_EXE` path inside it.

üxzView will be registered for your user in **Open with** and **Settings → Apps → Default apps**. Choose üxzView there for the image types you want. Windows requires you to make that final choice yourself.

For a registry file you can inspect or import manually:

```powershell
./register-windows.ps1 -ExecutablePath 'C:\Apps\üxzView\uxzView.exe' -GenerateOnly -RegFilePath './uxzView.reg'
```

Run `unregister-windows.bat` to remove üxzView's registration. If you move the application folder, register its new location again. No administrator rights are needed. [Registration details](docs/FILE_ASSOCIATIONS.md).

### Linux

Install üxzView and its desktop entry using the [build guide](docs/PLATFORMS.md). Your file manager's **Open with** menu can then select üxzView. To set common defaults from a terminal:

```sh
xdg-mime default org.ixview.IxView.desktop image/jpeg image/png image/webp image/gif image/tiff image/bmp
```

### macOS

Place the built `uxzView.app` in Applications and open it once. In Finder, select an image → **Get Info → Open with → üxzView → Change All**. Repeat for each image type you want to associate.

Development: [contributing](CONTRIBUTING.md), [architecture](docs/ARCHITECTURE.md), [tests and maintenance](docs/DEVELOPMENT.md).

## License

GPL-3.0-or-later. See [LICENSE](LICENSE). Third-party libraries keep their own licenses; Windows packages include license texts and [corresponding source information](docs/THIRD_PARTY.md).
