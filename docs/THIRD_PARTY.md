# Drittanbieter

üxzView verwendet dynamisch gelinkte Qt- und Codec-Bibliotheken. Die Versionen und SHA-256 der ursprünglichen SDK-Pakete sind in `scripts/sdk-lock.json` festgehalten. Bezugsquelle: signierte MSYS2-UCRT-Pakete von `repo.msys2.org`. Die Pakete werden in dieser Arbeitskopie projektlokal entpackt.

Wichtige Komponenten:

| Komponente | Verwendung | Lizenzfamilie (mitgelieferte Texte maßgeblich) |
| --- | --- | --- |
| Qt 6 Widgets/Gui/Core/Concurrent/PrintSupport/Svg/Network | native Oberfläche und Bild-I/O | LGPL-3.0 / GPL / Qt-Angebote je Modul |
| KDE KImageFormats, KArchive | zusätzliche Qt-Decoder/Encoder | LGPL-/BSD-Varianten je Quelldatei |
| libjpeg-turbo | JPEG und verlustfreie Koeffiziententransformation | BSD/IJG/zlib |
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

`scripts/dependency_sources.py --check-urls` erstellt zusätzlich `SOURCES.md` und `dependency-sources.json` im portablen Verzeichnis. Unveränderte Drittanbieter-DLLs werden anhand ihres SHA-256 mit einer Datei aus dem gesperrten SDK verglichen und dem ursprünglichen, ebenfalls hashgeprüften MSYS2-Paket zugeordnet. Für die beiden nachstehend beschriebenen Neubauten werden stattdessen Ausgabehash und dokumentierte Build-Herkunft geprüft. Die Metadaten liefern Paketbasis, Version, Lizenzangaben und PKGBUILD-Prüfsumme. Daraus entstehen genaue Links auf die passenden `.src.tar.zst` mit Quellcode, Patches und Build-Rezepten. Die Verfügbarkeitsprüfung gleicht alle Namen mit dem öffentlichen MSYS2-Quellpaketindex ab. Sie lädt nicht jedes Quellarchiv herunter und bestätigt deshalb dessen Inhalt nicht unabhängig.

Seit 0.4.3 werden libheif und libavif für das Windows-Paket aus festgelegten MSYS2-Quelldateien neu gebaut. `scripts/build_heif.py` und `scripts/build_avif.py` prüfen sowohl die Quellpakete als auch die enthaltenen Upstream-Archive per SHA-256 und verwenden das gesperrte SDK. Kvazaar übernimmt HEVC-Encoding, AOM das AV1-Encoding; die zusätzlichen x265-/SVT-AV1-/rav1e-Encoder entfallen. Die Decoder und die angebotenen Formatfamilien bleiben erhalten. Kompressionsverhalten und Encoderlaufzeiten können sich ändern; dies ist kein Nachweis identischer Encoderqualität. `custom-builds.json` und die entsprechenden Einträge in `dependency-sources.json` dokumentieren Quell- und Ausgabehash, Rezepte einschließlich gemeinsamem Build-Helfer, SDK-Lock und CMake-Optionen. Alle übrigen Drittanbieter-DLLs werden weiterhin direkt mit den SDK-Paketen verglichen. Die Rezepte sind Bestandteil des üxzView-Source-ZIP.

Die beiden Quellhinweisdateien liegen sowohl im Windows-ZIP als auch separat direkt beim Release-Download. Lizenztexte allein ersetzen die jeweiligen Quellcodepflichten nicht. Der Distributor muss die verlinkten Quellen verfügbar halten; vor einer Entfernung älterer MSYS2-Pakete eine eigene Kopie bereitstellen und die Hinweise aktualisieren. Das Verfahren und die Veröffentlichungsschritte stehen in [RELEASING.md](RELEASING.md). Grundlage sind der [MSYS2-Quellserver](https://www.msys2.org/dev/mirrors/) und [GPLv3 Abschnitt 6](https://www.gnu.org/licenses/gpl-3.0.html#section6).

Die eigene Toolbar-Symbolsammlung und das Programmsymbol stehen unter der Projektlizenz; es gibt dafür keine zusätzliche Icon-Font- oder Bildbibliothek.

Das Programmsymbol basiert auf dem von Michael Förtsch bereitgestellten Pixel-Logo (`resources/logo.png`); SVG-, ICO- und ICNS-Versionen bewahren dessen Formen. Die Toolbar verwendet eigene SVG-Konturen aus `src/ui/icons.cpp`. Diese Grafiken stehen unter der Projektlizenz.

`python scripts/build_icons.py` erzeugt die Plattform-Symbole neu. Pillow wird dafür nur als Entwicklungswerkzeug benötigt und gehört nicht zur Anwendungslaufzeit.

## Optional recognition tools

ZXing-C++ 3.0.0 and Tesseract 5.5.3 use Apache-2.0; Leptonica 1.85.0 uses its BSD-style license. The pinned tessdata_fast models include their Apache-2.0 license. The Windows package includes these engines/models unpacked under `tools`; separate `.ixtool` assets remain available for reinstallation. The QR fixture in `tests/extensions_ixview.cpp` derives from ZXing-C++ QREncoderTest (Copyright 2008 ZXing authors, Apache-2.0). Each pack includes applicable license texts and source URLs/hashes; Windows engine packs additionally record the compiler-runtime source packages. See `scripts/tool-sources-lock.json` and [tool extensions](TOOL_EXTENSIONS.md).
