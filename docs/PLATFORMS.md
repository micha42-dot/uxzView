# Plattformen und native Builds

üxzView verwendet C++20 und Qt Widgets auf Windows, Linux und macOS. **Zunächst wird ausschließlich eine fertige Windows-x64-Fassung veröffentlicht.** Alle vier GitHub-Jobs haben bereits kompiliert; der zuletzt veröffentlichte Stand `c795e2a` scheitert noch an Windows-/Linux-Testfällen und den macOS-Paketstartprüfungen. Die entsprechenden Korrekturen liegen lokal vor und brauchen einen neuen CI-Lauf. Der aktuelle Stand ist damit noch nicht auf allen Zielsystemen bestätigt; siehe [Build-Prüfung](BUILD_REVIEW_2026-10-27.md).

Alle Befehle werden im Wurzelverzeichnis eines Checkouts oder entpackten Quellpakets ausgeführt. Ein Windows-Build erzeugt keine Linux- oder macOS-Binärdatei; für diese Builds einen entsprechenden Host verwenden.

| System | Ziel / Build-Konfiguration | Hier nachgewiesen |
| --- | --- | --- |
| Windows | Windows 10 ab 1809 und neuer, x64; MSYS2-UCRT-SDK | Windows-11-Build, alle fünf Offscreen-Suiten und vierunddreißig native Fensterabläufe; portables Paket ohne SDK-Pfade |
| Linux | zunächst Ubuntu 24.04 x64; andere Distributionen mit Qt ≥ 6.8 und passenden Bibliotheken aus Quellcode | `c795e2a` kompiliert in CI; ein Testdatenfehler ist lokal korrigiert, aktueller CI-Lauf steht aus |
| macOS | zunächst macOS 15 und neuer, getrennte arm64-/Intel-Builds | `c795e2a` kompiliert und besteht CTest für beide CPUs; korrigierter Paketstart braucht einen neuen CI-Lauf |

Die Windows-Untergrenze entspricht der [Qt-6.11-Plattformmatrix](https://doc.qt.io/qt-6.11/supported-platforms.html) des gesperrten Windows-SDKs; der komplette üxzView-Build wurde bisher auf Windows 11 geprüft. Qt 6.8 selbst kann macOS ab 12 unterstützen; die hier vorbereiteten Mac-Pakete zielen zunächst auf 15, weil zusätzlich die Mindestversionen der Homebrew-Bibliotheken berücksichtigt werden müssen. Eine niedrigere Grenze benötigt einen passenden Build sämtlicher Abhängigkeiten und einen Test auf diesem System. Neue Betriebssystemversionen werden durch die gemeinsame Codebasis angestrebt, gelten ohne Test aber nicht automatisch als validiert.

## Abhängigkeiten

CMake ≥ 3.24, C++20-Compiler und Qt ≥ 6.8 mit Core, Gui, Widgets, Concurrent, PrintSupport und Svg. Qt Test und libtiff werden nur für `BUILD_TESTING=ON` benötigt. LittleCMS ist eine optionale Testabhängigkeit für unabhängige Farbreferenzen; ohne sie werden vier Fälle ausdrücklich übersprungen. libjpeg-turbo/TurboJPEG ist erforderlich. Exiv2 ist optional; fehlt es, meldet CMake dies und Metadaten-Erhaltung steht nicht zur Verfügung. `IX_ENABLE_METADATA=OFF` schaltet sie bewusst ab. Exiv2 0.27 und 0.28+ werden berücksichtigt.

Qt Network erzeugt das optionale Modul `ixextensions`, LibRaw (threadsafe `raw_r`) das optionale Modul `ixrawpreview`. Beide werden zur Laufzeit erst bei Bedarf geladen. Die CMake-Installation nimmt diese Module samt zusätzlicher Laufzeitabhängigkeiten in den Deployment-Schritt auf; diese neuen Unix-Deploymentpfade sind hier noch nicht nativ verifiziert.

`-DIX_REQUIRE_FULL_FEATURES=ON` verlangt Exiv2, LibRaw und Qt Network bereits beim Konfigurieren. Desktop-CI und Windows-Release-Vorbereitung verwenden diese Prüfung, damit eine fehlende Development-Bibliothek nicht unbemerkt eine eingeschränkte Fassung erzeugt. Für bewusst reduzierte Builds bleibt die Option standardmäßig aus. Unter PowerShell aktiviert `./scripts/build.ps1 -Test -RequireFullFeatures` dieselbe Prüfung.

Nach der Installation prüft `python3 scripts/verify_native_modules.py --stage dist/native` beide Zusatzmodule samt Einstiegspunkten über den Betriebssystem-Lader. Auf macOS führt der Prüfer dafür die App mit `--probe-modules` aus, damit die Bundle-Ladepfade zum richtigen Programm gehören. Die Unix-CI entfernt vorher SDK-Bibliothekspfade. Das prüft die ladbaren Module zusätzlich zum PNG-Starttest, ohne Erweiterungsdialoge oder Werkzeuge auszuführen. Auf Windows prüft derselbe Aufruf ohne `--stage` das portable Verzeichnis `dist/uxzView`.

TurboJPEG wird zuerst über sein CMake-Paket, dann über pkg-config und schließlich über Header/Bibliothek gesucht. Exiv2 unterstützt beide verbreiteten CMake-Targetnamen sowie pkg-config. Es werden keine Abhängigkeiten während einer normalen CMake-Konfiguration aus dem Netz geladen.

Die Formatmatrix `FORMAT_COMPATIBILITY.md` beschreibt **das gebaute Windows-Paket**. Decoder hängen auf Linux/macOS von den dort installierten Qt-/KImageFormats-Plugins ab. Qt Image Formats ergänzt unter anderem TIFF und WebP; KImageFormats ergänzt beispielsweise PSD und RAW. Die Tests verlangen die Basisformate; ein fehlendes optionales PSD-Plugin wird ausdrücklich als übersprungen protokolliert. Jeder Plattformlauf erzeugt seine eigene `tests/output/format-capabilities.json`. Die vorbereitete Unix-CI verwendet Qt Image Formats; das breite Windows-Pluginpaket ist damit noch nicht vollständig auf Unix nachgewiesen.

## Windows

Voraussetzungen: Windows x64, PowerShell und **Python 3.14+** (für Zstandard-Pakete). Git wird nur zum Klonen benötigt. Für diesen Weg sind Visual Studio, ein globales Qt und ein systemweites MSYS2 nicht erforderlich. Einmalig Internet für den Bootstrap und mehrere GiB freien Platz einplanen.

```powershell
python scripts/bootstrap_sdk.py
python scripts/build_tool_packs.py
./scripts/build.ps1 -Test
python scripts/format_matrix.py
python scripts/deploy.py
python scripts/verify_portable.py
python scripts/package.py
```

Das Ergebnis liegt unter `build/uxzView.exe`, die weitergebbare Laufzeit unter `dist/uxzView/uxzView.exe`. Die ausführbare Datei im Build-Ordner benötigt noch die SDK-DLLs im PATH. Zum normalen Start das vollständige portable Verzeichnis verwenden. Der erste öffentliche Windows-Release wird mit `scripts/prepare_release.ps1` erstellt; siehe [RELEASING.md](RELEASING.md).

`bootstrap_sdk.py` prüft alle Downloads gegen `scripts/sdk-lock.json` und installiert unter `.deps/sdk`. Die ursprünglichen Paketdateien bleiben in `.deps/cache`; für langfristige Reproduzierbarkeit aufbewahren. Ältere Pakete können upstream entfernt werden. Erneute Extraktion und Hash-Prüfung arbeiten mit vorhandenem Cache offline. Getestet wurden GCC 16.2.0, Qt 6.11.2, CMake 4.4.4 und Ninja 1.13.2.

Der Installer prüft vor dem Schreiben sämtliche Paket-Hashes und vergleicht vorhandene SDK-Dateien mit ihrem tatsächlichen Archivinhalt. Beschädigte Dateien werden auch bei unveränderter Größe wiederhergestellt; unveränderte Dateien behalten ihren Zeitstempel. Zusätzliche, nicht im Lockfile enthaltene Cache-Pakete werden nicht installiert. Die zugehörigen Offline-Regressionen laufen mit `python -m unittest discover -s tests -p test_sdk_bootstrap.py -v` sowie automatisch vor dem Windows-CI- und Release-Build.

Das SDK ist projektlokal; `CMakePresets.json` enthält `windows-x64`. Für andere Qt-SDKs ist das generische Preset `native` vorgesehen. Der lokale Paketierer erzeugt weiterhin `dist/uxzView/uxzView.exe` und `dist/uxzView-win64.zip`.

`deploy.py` baut libheif und libavif aus festgelegten, hashgeprüften MSYS2-Quellarchiven mit dem lokalen SDK neu. Der erste Durchlauf benötigt Internet; danach werden Downloads und geprüfte Build-Ergebnisse wiederverwendet. HEVC nutzt kvazaar, AV1 nutzt AOM. Das reduziert die Paketgröße, ohne die bisherigen Formatfamilien zu entfernen. Einzelheiten und Quellzuordnung: [THIRD_PARTY.md](THIRD_PARTY.md).

Die normale Testausführung bleibt offline. Für die fünf echten Kamera-RAWs einmal `python scripts/fetch_quality_fixtures.py` ausführen. Release-Vorbereitung und Windows-CI verlangen alle fünf Dateien. [Qualitätsnachweise](QUALITY.md) beschreiben Profile, HDR, Prüfsummen und Grenzen.

## Linux

Beispiel für Ubuntu 24.04 x64 mit GCC, Bash und Qt-SDK. Das Qt aus den Standardpaketen dieser Distribution ist zu alt; zusätzlich Qt ≥ 6.8 einschließlich Qt Svg und Qt Image Formats installieren. Ein Weg ist der Qt-Installer; alternativ der unten gezeigte [aqtinstall-Ablauf](https://aqtinstall.readthedocs.io/en/latest/getting_started.html).

Systembibliotheken und Build-Werkzeuge:

```sh
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build pkg-config python3-venv \
  libturbojpeg0-dev libexiv2-dev libtiff-dev libraw-dev liblcms2-dev libgl1-mesa-dev \
  libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 \
  libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libfontconfig1-dev desktop-file-utils
```

Qt 6.8.3 lokal installieren und bauen (diese Qt-Version verwendet auch die Unix-CI):

```sh
python3 -m venv .deps/aqt-env
.deps/aqt-env/bin/python -m pip install aqtinstall==3.3.0
.deps/aqt-env/bin/python -m aqt install-qt linux desktop 6.8.3 linux_gcc_64 \
  --outputdir .deps/Qt --modules qtimageformats \
  --archives qtbase qtsvg qttools qttranslations icu qtwayland
export PATH="$PWD/.deps/Qt/6.8.3/gcc_64/bin:$PATH"
bash scripts/build.sh "-DCMAKE_PREFIX_PATH=$PWD/.deps/Qt/6.8.3/gcc_64"
./build/native/uxzView
```

Alternativ bei einem bereits auffindbaren Qt ≥ 6.8: `cmake --preset linux`, `cmake --build --preset linux`, `ctest --preset linux`. Die Pakete mancher Distributionen liefern ältere Qt-Versionen; diese erfüllen die Mindestversion nicht.

Systeminstallation mit Bibliotheken aus der Distribution:

```sh
cmake --install build/native --prefix "$HOME/.local"
```

Die Installation enthält `bin/uxzView`, einen `.desktop`-Eintrag und das eigene SVG-Symbol. Dieser Ablauf setzt die passenden Bibliotheken/Plugins im Zielsystem voraus.

## macOS

Beispiel für macOS 15+ mit Xcode Command Line Tools und Homebrew. Einmalig `xcode-select --install` ausführen, falls noch kein Apple-Compiler vorhanden ist. Homebrew passend zur Prozessorarchitektur installieren: arm64 auf Apple Silicon, x86_64 auf Intel. Im nativen Terminal des jeweiligen Macs:

```sh
brew install cmake ninja pkg-config qt jpeg-turbo exiv2 libtiff libraw little-cms2
bash scripts/build.sh \
  "-DCMAKE_PREFIX_PATH=$(brew --prefix qt);$(brew --prefix jpeg-turbo);$(brew --prefix exiv2);$(brew --prefix libtiff)" \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0
open build/native/uxzView.app
```

`macos-arm64` und `macos-x86_64` sind getrennte Presets. Alle Bibliotheken müssen zur gewählten Architektur passen; diese Presets allein machen aus einer Homebrew-Installation kein Universal-Binary. Das Ergebnis ist `build/native/uxzView.app`. Das Bundle deklariert Bilddokumente und empfängt Finder-Dateiöffnungen über `QFileOpenEvent`. Die systemweite Standardzuordnung wird nicht ungefragt geändert.

## Build-Varianten und Fehlerbehebung

Ohne Tests sind Qt Test und libtiff als direkte Testabhängigkeit nicht erforderlich:

```sh
cmake -S . -B build/minimal -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=OFF -DCMAKE_PREFIX_PATH=/pfad/zum/Qt-SDK
cmake --build build/minimal --parallel 4
```

`-DIX_ENABLE_METADATA=OFF` baut ohne Exiv2-Funktionen. Qt Network und LibRaw werden automatisch erkannt; ihr Fehlen reduziert den Funktionsumfang. Ein Vollausbau benötigt beide. Der gesperrte Windows-SDK enthält alle Komponenten.

| Meldung / Problem | Abhilfe |
| --- | --- |
| Qt6Config.cmake nicht gefunden / Version zu alt | `CMAKE_PREFIX_PATH` auf das SDK-Verzeichnis mit `lib/cmake/Qt6` setzen; Qt ≥ 6.8 verwenden. |
| TurboJPEG nicht gefunden | Development-Paket installieren; auf macOS `brew --prefix jpeg-turbo` zusätzlich im Prefix angeben. |
| Generator / Compiler im Cache widersprüchlich | Einen neuen Build-Ordner wählen. ABI-kompatible Qt-/Compiler-/Architektur-Kombination verwenden. |
| Kein Display im CI-Test | CTest setzt `QT_QPA_PLATFORM=offscreen`; direkt gestartete Testprogramme mit diesem Wert ausführen. |
| Windows-DLL fehlt | `dist/uxzView` vollständig verwenden, nicht nur die EXE kopieren. |
| Weniger Bildformate als im Windows-Paket | Passende Qt-Image-I/O-Plugins installieren; unter Hilfe die tatsächlichen Decoder prüfen. |
| Unix-Skript meldet `bash\r` | Zeilenenden auf LF korrigieren; `.gitattributes` verhindert dies beim regulären Checkout. |

## Native Entwicklungspakete und CI

```sh
# Bereits beim Konfigurieren aktivieren; derselbe native Build-Ordner:
cmake -S . -B build/native -DIX_DEPLOY_QT=ON
cmake --build build/native --parallel 4
ctest --test-dir build/native --output-on-failure
cmake --install build/native --prefix "$PWD/dist/native"
python3 scripts/package_native.py --name uxzView-linux-x86_64
```

`IX_DEPLOY_QT` verwendet [Qt Deployment für native Desktop-Builds](https://doc.qt.io/qt-6.8/qt-generate-deploy-app-script.html). Die Option ist standardmäßig aus; aktivierte Paketierung bündelt Laufzeitbibliotheken und Plugins. macOS benötigt dafür einen Mac, Linux einen Linux-Host. Windows kann weiterhin den ausführlich geprüften projektspezifischen Paketierer verwenden.

`.github/workflows/desktop.yml` bereitet vier Jobs vor: Windows x64, Linux x64, macOS arm64 und macOS Intel. Windows nutzt den vorhandenen SDK-Lock; Unix nutzt Qt 6.8.3 und Systembibliotheken. Diese sind versionsabhängig vom Runner und deshalb noch nicht vollständig per Hash gesperrt. Die Jobs bauen, testen, installieren und prüfen das installierte Programm mit `--version` sowie einem echten PNG-Decodierlauf über `--probe-image`; Artefakte enthalten die jeweiligen Testprotokolle und Codec-Fähigkeiten. Diese Artefakte dienen Entwicklung und Prüfung und werden nicht automatisch als Releases veröffentlicht. `release-windows.yml` erstellt ausschließlich nach manuellem Start einen Windows-Release-Entwurf. Ein angelegter Workflow ist **kein erfolgreich ausgeführter CI-Lauf**. Hier wurde kein Repository hochgeladen und kein externer Lauf ausgelöst.

Die Mac-Pakete sind noch nicht mit Developer ID signiert oder notarisiert. Vor einer öffentlichen Veröffentlichung stehen außerdem die Prüfung des vollständigen Lizenz-/Quellpakets aller gebündelten Bibliotheken und echte Desktop-Tests aus. Die CI-Artefakte sind Entwicklungspakete.

## Plattformverhalten und offene Tests

- Die verifizierte Menüfolge bleibt im Fenster erhalten, auch auf macOS. Die klassische Qt-Windows-Optik verwendet auf Nicht-Windows-Systemen verfügbare Systemschriften. Layout/DPI müssen auf den realen Desktops noch geprüft werden.
- `QSettings` verwendet auf Windows die Registry, auf Linux Konfigurationsdateien und auf macOS die Systemeinstellungen der Anwendung. Pfade und Papierkorb werden über Qt verarbeitet.
- Bildschirmaufnahme nutzt derzeit `QScreen::grabWindow`. Unter Wayland fehlt noch der Portal-Ablauf für eine globale Bildschirmaufnahme; der Fehler nennt diese Grenze. macOS kann eine Bildschirmaufnahme-Berechtigung benötigen. Der Bildbetrachter selbst benötigt diese Berechtigung nicht.
- Drucken, Zwischenablage, Drag-and-drop, DPI, Dateidialoge, Farbmanagement und Finder-/Desktop-Start benötigen native Tests. Die Offscreen-Suite ersetzt sie nicht.

## Windows-10-Prüfung

`.github/workflows/windows10.yml` ist ein manuell startbarer Ablauf für einen selbst gehosteten Runner mit den Labels `self-hosted`, `Windows`, `X64`, `ixview-windows10`. Er verlangt ein Windows-10-Desktop-System ab Build 17763 und unter 22000; Windows Server und Windows 11 werden abgewiesen. Er erstellt das portable Paket, führt außerdem den gemeinsamen nativen Prüfablauf aus `scripts/validate.py` mit vier DPI-Skalierungen aus und lädt Betriebssystemdaten sowie Testprotokolle hoch. Der Runner muss dafür in einer interaktiven Benutzersitzung laufen; ein Dienst ohne Desktopzugriff genügt nicht. Der gehostete Windows-Server-Job ersetzt diesen Nachweis nicht.

Hier ist Windows 11 verfügbar. Die fünf vollständigen Offscreen-Suiten, das portable Paket und vierunddreißig gezielte native Fensterabläufe wurden darauf geprüft. Ein Windows-10-Host, ein Linux-System und ein Mac stehen in dieser Arbeitsumgebung nicht zur Verfügung; WSL besitzt keine installierte Distribution. Diese drei nativen Nachweise können deshalb erst auf entsprechenden Hosts beziehungsweise nach Einrichtung des Repositorys ausgeführt werden.


Native Archive schließen persönliche Portabelprofile und die appnahen Tools-Ordner aus. Das Mitliefern der vorkonfigurierten OCR-/QR-Pakete ist derzeit nur im Windows-Paket eingerichtet; die native Paketerstellung übernimmt keine lokal installierten Werkzeuge.
