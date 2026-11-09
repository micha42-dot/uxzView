# Drittanbieter

üxzView verwendet Qt- und Codec-Bibliotheken sowie die nachstehend genannten Funktionserweiterungen. jpegli und sein Highway-Code sind statisch eingebaut; die übrigen Windows-Bibliotheken werden dynamisch geladen. Versionen und SHA-256 der SDK-Pakete stehen in `scripts/sdk-lock.json`, die nativen Quellrevisionen in `scripts/native-sources-lock.json` und die Erkennungspakete in `scripts/tool-sources-lock.json`.

Das mit jedem Windows-Paket ausgelieferte **`SOURCES.md`** listet sämtliche ausgelieferten Drittanbieter-DLL-Pakete mit Version, Lizenz, Upstream-Projekt und Quellarchiv auf, anschließend die statisch eingebauten Komponenten, Erkennungswerkzeuge und Sprachmodelle. Die vollständigen Paket-Lizenztexte liegen unter **`licenses`** und bei den jeweiligen Werkzeugpaketen unter **`tools`**. `dependency-sources.json` und `native-dependencies.json` ordnen die Dateien ihren geprüften Quellen zu. Neue Bibliotheken oder Erweiterungen benötigen einen Lizenz-/Quelleneintrag vor der Auslieferung.

Wichtige Komponenten:

| Komponente | Verwendung | Lizenzfamilie (mitgelieferte Texte maßgeblich) |
| --- | --- | --- |
| Qt 6 Widgets/Gui/Core/Concurrent/PrintSupport/Svg/Network | native Oberfläche und Bild-I/O | LGPL-3.0 / GPL / Qt-Angebote je Modul |
| KDE KImageFormats, KArchive | zusätzliche Qt-Decoder/Encoder | LGPL-/BSD-Varianten je Quelldatei |
| libjpeg-turbo | JPEG und verlustfreie Koeffiziententransformation | BSD/IJG/zlib |
| [Google jpegli](https://github.com/google/jpegli) | fest eingebauter JPEG-Encoder für Speichern und Batch | BSD-3-Clause |
| [Google Highway](https://github.com/google/highway) | SIMD-Bausteine für jpegli und JPEG XL | Apache-2.0 oder BSD-3-Clause; Sonderlizenzen siehe vollständiger Text |
| libpng, zlib | PNG und APNG-Pixel | libpng/zlib |
| libtiff, libwebp | TIFF, WebP | BSD-Varianten |
| libavif, AOM/dav1d | AVIF | BSD-/AOM-Varianten |
| libheif, libde265, kvazaar und weitere Codec-Abhängigkeiten | HEIF/HEIC | LGPL-/GPL-/BSD-Varianten; konkrete Pakettexte beachten |
| libjxl, Highway | JPEG XL | BSD-/Apache-Varianten |
| OpenJPEG, JasPer | JPEG 2000 | BSD-/JasPer-Varianten |
| OpenEXR, Imath | OpenEXR | BSD-Varianten |
| LibRaw | Kamera-RAW | LGPL-/CDDL-Angebote gemäß Paket |
| Exiv2 | EXIF/IPTC/XMP | GPL-2.0-or-later |
| GCC-/MinGW-Laufzeiten | Windows-x64-Laufzeit | GPL mit Runtime-Ausnahmen / MinGW-Lizenzen |

`deploy.py` kopiert die von MSYS2 mitgelieferten Lizenzverzeichnisse unter `dist/uxzView/licenses`. Der eigene Quellcode ist GPL-3.0-or-later; das Release enthält den zugehörigen üxzView-Quellcode und die Buildskripte.

`scripts/dependency_sources.py --check-urls` erstellt zusätzlich `SOURCES.md` und `dependency-sources.json` im portablen Verzeichnis. Unveränderte Drittanbieter-DLLs werden anhand ihres SHA-256 mit einer Datei aus dem gesperrten SDK verglichen und dem ursprünglichen, ebenfalls hashgeprüften MSYS2-Paket zugeordnet. Für die nachstehend beschriebenen Neubauten werden stattdessen Ausgabehash und dokumentierte Build-Herkunft geprüft. Die Metadaten liefern Paketbasis, Version, Lizenzangaben und PKGBUILD-Prüfsumme. Daraus entstehen genaue Links auf die passenden `.src.tar.zst` mit Quellcode, Patches und Build-Rezepten. Die Verfügbarkeitsprüfung gleicht alle Namen mit dem öffentlichen MSYS2-Quellpaketindex ab. Sie lädt nicht jedes Quellarchiv herunter und bestätigt deshalb dessen Inhalt nicht unabhängig.

Seit 0.4.3 werden libheif und libavif für das Windows-Paket aus festgelegten MSYS2-Quelldateien neu gebaut. `scripts/build_heif.py` und `scripts/build_avif.py` prüfen sowohl die Quellpakete als auch die enthaltenen Upstream-Archive per SHA-256 und verwenden das gesperrte SDK. Kvazaar übernimmt HEVC-Encoding, AOM das AV1-Encoding; die zusätzlichen x265-/SVT-AV1-/rav1e-Encoder entfallen. Die Decoder und die angebotenen Formatfamilien bleiben erhalten. Kompressionsverhalten und Encoderlaufzeiten können sich ändern; dies ist kein Nachweis identischer Encoderqualität. `custom-builds.json` und die entsprechenden Einträge in `dependency-sources.json` dokumentieren Quell- und Ausgabehash, Rezepte einschließlich gemeinsamem Build-Helfer, SDK-Lock und CMake-Optionen. Alle übrigen Drittanbieter-DLLs werden weiterhin direkt mit den SDK-Paketen verglichen. Die Rezepte sind Bestandteil des üxzView-Source-ZIP.

Die beiden Quellhinweisdateien liegen sowohl im Windows-ZIP als auch separat direkt beim Release-Download. Lizenztexte allein ersetzen die jeweiligen Quellcodepflichten nicht. Der Distributor muss die verlinkten Quellen verfügbar halten; vor einer Entfernung älterer MSYS2-Pakete eine eigene Kopie bereitstellen und die Hinweise aktualisieren. Das Verfahren und die Veröffentlichungsschritte stehen in [RELEASING.md](RELEASING.md). Grundlage sind der [MSYS2-Quellserver](https://www.msys2.org/dev/mirrors/) und [GPLv3 Abschnitt 6](https://www.gnu.org/licenses/gpl-3.0.html#section6).

Die eigene Toolbar-Symbolsammlung und das Programmsymbol stehen unter der Projektlizenz; es gibt dafür keine zusätzliche Icon-Font- oder Bildbibliothek.

Das Programmsymbol basiert auf den von Michael Förtsch bereitgestellten transparenten Pixel-Logos (`resources/logo-light.png` für helle und `resources/logo-dark.png` für dunkle Oberflächen); SVG-, ICO- und ICNS-Versionen bewahren deren Formen und Transparenz. Die App wechselt ihre Logo-Variante mit dem wirksamen Oberflächentheme. Das feste Desktop-/Dateisymbol verwendet die graue Variante für beide Hintergründe. Die Toolbar verwendet eigene SVG-Konturen aus `src/ui/icons.cpp`. Diese Grafiken stehen unter der Projektlizenz.

`python scripts/build_icons.py` erzeugt die Plattform-Symbole neu. Pillow wird dafür nur als Entwicklungswerkzeug benötigt und gehört nicht zur Anwendungslaufzeit.

## Optional recognition tools

[ZXing-C++](https://github.com/zxing-cpp/zxing-cpp) 3.0.0 and [Tesseract](https://github.com/tesseract-ocr/tesseract) 5.5.3 use Apache-2.0; [Leptonica](https://github.com/DanBloomberg/leptonica) 1.85.0 uses its BSD-style license. The pinned [tessdata_fast](https://github.com/tesseract-ocr/tessdata_fast) models include their Apache-2.0 license. The Windows package includes these engines/models unpacked under `tools`; separate `.ixtool` assets remain available for reinstallation. The QR fixture in `tests/extensions_ixview.cpp` derives from ZXing-C++ QREncoderTest (Copyright 2008 ZXing authors, Apache-2.0). Each pack includes applicable license texts and source URLs/hashes; Windows engine packs additionally record the compiler-runtime source packages. See `scripts/tool-sources-lock.json` and [tool extensions](TOOL_EXTENSIONS.md).

## Built-in jpegli

`cmake/Jpegli.cmake` builds Google jpegli source revision
`031a0077f5799a6041004267fc12b956c1f52a20`, its pinned Highway dependency and the
matching libjpeg62 API headers. The headers come from the libjpeg-turbo revision
recorded in `native-sources-lock.json`; no second libjpeg DLL is installed.
The encoder calls unique `jpegli_*` symbols, while existing JPEG decoding and
coefficient transforms retain libjpeg-turbo. JPEG XL retains its own codec.

This software is based in part on the work of the Independent JPEG Group.

The unmodified jpegli `LICENSE`, `AUTHORS`, `PATENTS`, Highway `LICENSE`, and
libjpeg-turbo `LICENSE.md`/`README.ijg` are copied from hash-verified source
archives into `licenses/native`. Native macOS installation places these inside
the app bundle; Linux uses `share/IxView/licenses/native`. Windows additionally
records every notice hash and the application hash in `native-dependencies.json`.
Release validation rejects missing/changed native notices, a changed source lock
or an application that differs from that inventory.

## Build and CI tools

These tools prepare or verify the project. Windows compiler/tool versions are
fixed by the SDK lock; CI action versions are in `.github/workflows`.

| Tool / repository | Use | License |
| --- | --- | --- |
| [CMake](https://github.com/Kitware/CMake) | Configure, compile and install | BSD-3-Clause |
| [Ninja](https://github.com/ninja-build/ninja) | Build execution | Apache-2.0 |
| [CPython](https://github.com/python/cpython) | SDK, packaging and validation scripts | PSF-2.0 and bundled component notices |
| [Pillow](https://github.com/python-pillow/Pillow) | Regenerating logo assets | MIT-CMU / PIL notices |
| [GCC](https://gcc.gnu.org/), [MinGW-w64](https://www.mingw-w64.org/), [MSYS2](https://www.msys2.org/) | Windows compiler, runtime and verified packages | GCC GPL / Runtime Library Exception; package-specific MinGW/MSYS2 licenses |
| [aqtinstall](https://github.com/miurahr/aqtinstall), [install-qt-action](https://github.com/jurplel/install-qt-action) | Native CI Qt SDK installation | MIT |
| [checkout](https://github.com/actions/checkout), [setup-python](https://github.com/actions/setup-python), [cache](https://github.com/actions/cache), [upload-artifact](https://github.com/actions/upload-artifact) | GitHub CI checkout, setup, cache and artifacts | MIT; each action retains its own bundled notices |
| [Git](https://github.com/git/git) | Source checkout and version control | GPL-2.0 |

Other compiler/system packages on Linux and macOS retain the license notices of
their platform distributors. Runtime source/notice auditing for native
Linux/macOS release artifacts is separate from the verified Windows inventory.

## PDF reading

Qt PDF/PDFium is bundled as a lazy runtime dependency. Only Qt PDF core and the
project reader module are deployed, without Qt PDF Quick/Widgets or a browser
process. Qt PDF and bundled PDFium/font/image components retain their own license
texts under `licenses/qt6-pdf`; exact binary/source origins are recorded in
`dependency-sources.json` and `SOURCES.md`. See [Qt PDF licensing](https://doc.qt.io/qt-6.8/qtpdf-licensing.html).

## Local-file Exiv2 build

The Windows deployment also rebuilds the pinned Exiv2 0.28.9 source with HTTP/Curl
readers disabled. Metadata input is local-file based; extension downloads continue
through Qt Network. XMP, PNG, BMFF/Brotli, video metadata and NLS remain enabled.
`build_exiv2.py` records source/output hashes and options through the shared verified
builder. Gettext tools are SDK build dependencies and are not deployed. Runtime
OpenSSL libraries are retained explicitly for Qt's dynamically loaded TLS backend.
Windows ships the native Network List Manager plugin; the alternative GIO backend
and its exclusive dependencies are omitted. No image-format plugin is removed.
