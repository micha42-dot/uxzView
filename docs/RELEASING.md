# Windows-Release vorbereiten

Der erste fertige Download ist **Windows x64**. Linux und macOS erhalten zunächst den Quellcode mit [Build-Anleitung](PLATFORMS.md). Ihre CI-Artefakte werden nicht als Releases veröffentlicht. Der Release-Workflow legt einen **Entwurf** an; veröffentlicht wird dieser anschließend auf GitHub.

## Lokal bauen und paketieren

Im Projektverzeichnis mit Windows x64 und Python 3.14+:

```powershell
# Bei frischem Checkout einschließlich SDK-Download:
./scripts/prepare_release.ps1 -BootstrapSDK

# Bei bereits eingerichtetem SDK stattdessen:
# ./scripts/prepare_release.ps1
```

Das Skript prüft zunächst SDK-Installer, Übersetzungen und Registry-Erzeugung offline. Es lädt beziehungsweise prüft die fünf festgelegten CC0-Kamera-RAWs, baut außerdem die separaten QR-/OCR-Werkzeugpakete und Sprachmodelle, baut Release mit aktivierten Tests und führt alle fünf CTest-Suiten aus. Anschließend erzeugt es die Formatmatrix, baut die portable Laufzeit frisch auf und wiederholt alle fünf Suiten mit ausschließlich Windows-Systemverzeichnissen im PATH. Die HEIF-/AVIF-Neubauten benötigen beim ersten Lauf ihre hashgeprüften Quellarchive und verwenden das gesperrte SDK. Danach ordnet der Ablauf unveränderte DLLs den SDK-Paketen und die zwei Neubauten ihren dokumentierten Quellen/Rezepten zu. Die Verfügbarkeit aller zugehörigen Quellpakete wird im MSYS2-Index geprüft. Netzwerk wird für fehlende Quellen/RAW-Fixtures und die abschließende Verfügbarkeitsprüfung benötigt. Quellen werden mit dem lokalen Compiler gebaut; heruntergeladene Testbilder werden ausschließlich dekodiert.

Unter `dist` entstehen bei Version 0.4.4:

| Datei | Zweck |
| --- | --- |
| `uxzView-win64.zip` | Vollständiges portables Windows-Programm, Lizenztexte, Quellhinweise und eingebettetes üxzView-Quell-ZIP |
| `uxzView-source.zip` | Öffentlicher üxzView-Quellcode und Buildskripte für alle drei Systeme |
| `SOURCES.md` | Direkte Links zu den genauen Drittanbieter-Quellpaketen einschließlich Patches/Build-Rezepten |
| `dependency-sources.json` | DLL-Prüfsummen, Originalpakete, Versionen, Lizenzeinträge und Quell-URLs |
| `SHA256SUMS.txt` | SHA-256 der vier vorstehenden Dateien |
| `release-notes.md` | Vorbereiteter Veröffentlichungstext |

Alle fertigen Dateien liegen direkt unter `dist`, neben dem entpackten Programm `dist/uxzView`. `scripts/package.py` ist der gemeinsame Paketierer; der Release-Ablauf ruft ihn mit `--release` auf, um zusätzlich die Quellzuordnung zu prüfen und Veröffentlichungstext/Quellhinweise zu erzeugen. Beide Abläufe verwenden dieselben ZIP-Dateien `uxzView-win64.zip` und `uxzView-source.zip`. Erneute Paketierung aktualisiert diese Dateien; es gibt kein zusätzliches Release-Verzeichnis oder separates GitHub-ZIP. Die Versionsnummer im Veröffentlichungstext und im Git-Tag stammt aus `CMakeLists.txt`. `SHA256.json` bleibt als maschinenlesbare Prüfsummendatei für die lokalen/CI-Artefakte erhalten.

Prüfsumme unter Windows beispielsweise mit `Get-FileHash .\dist\uxzView-win64.zip -Algorithm SHA256` gegen `SHA256SUMS.txt` vergleichen. SHA-256 sichert Integrität; die Entwicklungsfassung ist noch nicht mit einem Windows-Code-Signing-Zertifikat signiert.

## Auf GitHub einen Entwurf erzeugen

Nach dem ersten Push und einer Prüfung des gewünschten Commits:

```powershell
git tag -a v0.4.4 -m "üxzView 0.4.4"
git push origin v0.4.4
```

Dann **Actions → Prepare Windows release draft → Run workflow** auswählen und den vorhandenen Tag `v0.4.4` angeben. Tag, CMake-Version und ausgecheckter Commit müssen übereinstimmen. Der Workflow baut den Tag neu, prüft ihn und legt mit dem eingebauten `GITHUB_TOKEN` einen Release-Entwurf an. Ein zusätzliches persönliches Token ist nicht erforderlich. Nur dieser Job erhält `contents: write`; normale Build-Jobs erhalten `contents: read`. Ein Push oder Tag allein veröffentlicht nichts.

Den Entwurf unter Releases öffnen. Windows-ZIP frisch in ein leeres Verzeichnis entpacken und starten; Öffnen, Rahmenzoom, Pfeiltasten, Speichern, Vorschauen und Einstellungen sowie einen Batchlauf an Testkopien prüfen. Den tatsächlichen Testumfang im Text korrekt benennen, insbesondere Windows-10- und native Desktop-Tests. Alle fünf Download-Anhänge einschließlich Quellhinweisen und Prüfsummen beibehalten und erst dann **Publish release** wählen. Ein wiederholter Workflow für einen bereits vorhandenen Release schlägt fehl, statt dessen Dateien still zu ersetzen.

Alternativ lokal erstellte Dateien manuell unter Releases → Draft a new release hochladen: die beiden ZIPs, `SOURCES.md`, `dependency-sources.json`, `SHA256SUMS.txt`; den Inhalt von `release-notes.md` als Beschreibung einsetzen. Keine Linux-/macOS-Entwicklungspakete hinzufügen.

## Quellen dauerhaft verfügbar halten

Die DLL-Zuordnung vergleicht unveränderte Dateien mit dem SDK und prüft die SHA-256 der ursprünglichen Binärpakete. Für libheif/libavif werden zusätzlich Quellhashes, Ausgabehashes, Rezepte samt gemeinsamem Build-Helfer, Optionen und SDK-Lock des Neubaus geprüft. Die Indexprüfung bestätigt die Existenz passender Versionsnamen; sie ersetzt keine vollständige Inhaltsprüfung jedes Quellarchivs. Die Quellhinweise müssen direkt beim Binärdownload verfügbar bleiben. MSYS2 kann ältere Pakete entfernen: die referenzierten `.src.tar.zst` rechtzeitig spiegeln und die Downloadhinweise aktualisieren. Die Verantwortung für die Verfügbarkeit bleibt beim Distributor. Details: [THIRD_PARTY.md](THIRD_PARTY.md), [MSYS2-Quellserver](https://www.msys2.org/dev/mirrors/) und [GPLv3 Abschnitt 6](https://www.gnu.org/licenses/gpl-3.0.html#section6).

Der Ablauf entspricht dem dokumentierten [Erstellen von Release-Entwürfen mit GitHub CLI](https://cli.github.com/manual/gh_release_create). Hier lokal erzeugte Artefakte sind noch kein hochgeladenes oder veröffentlichtes GitHub-Release.

Beide Werkzeuge und alle fünf OCR-Sprachen liegen einsatzbereit unter `dist/uxzView/tools` und sind im Windows-ZIP enthalten. `dist/tools/` enthält zusätzlich die einzelnen `.ixtool`-Downloads zur erneuten Installation. Der Katalog `uxzView-tools.json` und `uxzView-tools-SHA256SUMS.txt` gehören dazu. Dies ist ein getrennt installierbares Funktionspaket, keine zweite Kopie der App. Der Release-Workflow hängt diese Dateien als zusätzliche Assets an den Entwurf. Für einen HTTPS-Katalog seine tatsächliche Adresse in der Erweiterungsverwaltung eintragen. Details und Quellen: [TOOL_EXTENSIONS.md](TOOL_EXTENSIONS.md).

Die Paketierung verwendet den hashgeprüften Werkzeug-Snapshot `build/bundled-tools`, nicht den persönlichen Zustand unter `dist/uxzView/tools`. Deployment erhält dortige Deaktivierungen, Entfernungen und zusätzliche Pakete. Auslieferungsbudget: 160 MB für den Betrachter, zusätzlich 25 MB für die enthaltenen Erkennungswerkzeuge und Sprachmodelle.
