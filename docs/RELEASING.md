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

Das Skript prüft zunächst SDK-Installer, Übersetzungen und Registry-Erzeugung offline. Es lädt beziehungsweise prüft die fünf festgelegten CC0-Kamera-RAWs, baut außerdem die separaten QR-/OCR-Werkzeugpakete und Sprachmodelle, baut Release mit aktivierten Tests und führt alle acht CTest-Suiten aus. Es prüft die Windows-Versionsressource, erzeugt die Formatmatrix, baut die portable Laufzeit frisch auf und wiederholt alle acht Suiten mit ausschließlich Windows-Systemverzeichnissen im PATH. Die HEIF-/AVIF-Neubauten benötigen beim ersten Lauf ihre hashgeprüften Quellarchive und verwenden das gesperrte SDK. Danach ordnet der Ablauf unveränderte DLLs den SDK-Paketen und die zwei Neubauten ihren dokumentierten Quellen/Rezepten zu. Die Verfügbarkeit aller zugehörigen Quellpakete wird im MSYS2-Index geprüft. Netzwerk wird für fehlende Quellen/RAW-Fixtures und die abschließende Verfügbarkeitsprüfung benötigt. Quellen werden mit dem lokalen Compiler gebaut; heruntergeladene Testbilder werden ausschließlich dekodiert.

Unter `dist` entstehen:

| Datei | Zweck |
| --- | --- |
| `uxzView-win64.zip` | Vollständiges portables Windows-Programm, Lizenztexte, Quellhinweise und eingebettetes üxzView-Quell-ZIP |
| `uxzView-source.zip` | Öffentlicher üxzView-Quellcode und Buildskripte für alle drei Systeme |
| `SOURCES.md` | Direkte Links zu den genauen Drittanbieter-Quellpaketen einschließlich Patches/Build-Rezepten |
| `dependency-sources.json` | DLL-Prüfsummen, Originalpakete, Versionen, Lizenzeinträge und Quell-URLs |
| `SHA256SUMS.txt` | SHA-256 der vier vorstehenden Dateien |
| `release-notes.md` | Vorbereiteter Veröffentlichungstext |

Bei einem bereits zugeordneten, unveränderten Drittanbieter-Runtime lässt sich die 24-Stunden-Prüfung ohne erneute SDK-Archivdownloads auffrischen. Nach `deploy.py` die Zuordnung des letzten Pakets übernehmen und vollständig gegen die aktuellen Binärdateien prüfen lassen:

```powershell
Copy-Item dist/dependency-sources.json dist/uxzView/dependency-sources.json
Copy-Item dist/SOURCES.md dist/uxzView/SOURCES.md
python scripts/dependency_sources.py --refresh-urls
python scripts/package.py --release
```

`--refresh-urls` prüft zuerst eigene Build-Artefakte, sämtliche zugeordneten DLL-Hashes, SDK-Lock und Build-Rezepte. Nur bei Übereinstimmung wird der offizielle MSYS2-Quellindex erneut abgefragt. Geänderte Bibliotheken benötigen die vollständige Zuordnung mit `--check-urls` und den gesperrten SDK-Archiven. Ein fehlgeschlagener Quellenabruf aktualisiert keine Prüfdaten; die normale Release-Paketierung verlangt weiterhin eine erfolgreiche Prüfung innerhalb der letzten 24 Stunden.

Alle fertigen Dateien liegen direkt unter `dist`, neben dem entpackten Programm `dist/uxzView`. `scripts/package.py` ist der gemeinsame Paketierer; der Release-Ablauf ruft ihn mit `--release` auf, um zusätzlich die Quellzuordnung zu prüfen und Veröffentlichungstext/Quellhinweise zu erzeugen. Beide Abläufe verwenden dieselben ZIP-Dateien `uxzView-win64.zip` und `uxzView-source.zip`. Erneute Paketierung aktualisiert diese Dateien; es gibt kein zusätzliches Release-Verzeichnis oder separates GitHub-ZIP. Die Versionsnummer im Veröffentlichungstext und im Git-Tag stammt aus `CMakeLists.txt`. `SHA256.json` bleibt als maschinenlesbare Prüfsummendatei für die lokalen/CI-Artefakte erhalten.

Prüfsumme unter Windows beispielsweise mit `Get-FileHash .\dist\uxzView-win64.zip -Algorithm SHA256` gegen `SHA256SUMS.txt` vergleichen. SHA-256 sichert Integrität; die Entwicklungsfassung ist noch nicht mit einem Windows-Code-Signing-Zertifikat signiert.

## Windows-Identität, Signatur und Virenscanner

`resources/windows/app.rc.in` enthält die Produktdaten und übernimmt die Version
aus CMake. Der Sprach-/Codepage-Verweis muss zur String-Tabelle passen.
`verify_windows_metadata.py` liest diese Verweise sowie feste und textuelle
Versionen über die Windows-API; es startet die untersuchte EXE nicht:

```powershell
python scripts/verify_windows_metadata.py build/uxzView.exe
```

Für eine außerhalb des Quellbaums abgelegte Kopie kann `--version 0.5.0.0`
explizit angegeben werden. Die Prüfung bestätigt Ressourcen-Konsistenz, keine
Authenticode-Signatur und kein Scannerurteil.
Die Verweisregeln beschreibt Microsoft bei
[VerQueryValue](https://learn.microsoft.com/en-us/windows/win32/api/winver/nf-winver-verqueryvaluew).

Der automatisierte Release-Ablauf signiert bisher nicht. Sobald ein eigenes
Code-Signing-Zertifikat verfügbar ist, die eigenen fertigen Build-Ausgaben
**vor Deployment, Herkunftszuordnung und Paketierung** signieren. Den
Signierschritt nach dem Build und vor `deploy.py` einfügen; kein erneuter Build
danach. Werkzeugpakete behalten ihre separat geprüften Payload-Hashes.
Mit dem Windows-SDK-SignTool beispielsweise, mit tatsächlichem Zertifikat und
dem vom Signaturanbieter dokumentierten Zeitstempeldienst:

```powershell
signtool sign /sha1 CERTIFICATE_THUMBPRINT /fd SHA256 /tr TIMESTAMP_URL /td SHA256 build/uxzView.exe
signtool verify /pa /all /v build/uxzView.exe
```

Eigene mitgelieferte Module ebenso signieren und prüfen, wenn vorhanden:
`build/ixextensions.dll`, `build/ixrawpreview.dll`, `build/ixpdf.dll` und
`build/imageformats/ixarchive.dll`. Schlüssel und Zertifikatpasswörter gehören
nicht ins Repository oder in Befehlsprotokolle. Die Zertifikatsauswahl und
SHA-256-/Zeitstempeloptionen folgen dem
[SignTool-Vertrag](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).
Nach dem Signieren Deployment/portable Tests erneut ausführen und erst danach
die endgültigen ZIP-Prüfsummen erzeugen. Ohne eingerichtetes Zertifikat bleibt
der Download als unsigniert beschrieben. Eine gültige Signatur bescheinigt den
Herausgeber und die Integrität; sie ersetzt keine Sicherheitsprüfung.

Für einen gemeldeten Fund den vollständigen Fundnamen, den exakten Datei-SHA-256,
Scanner-/Definitionsversion, Zeitpunkt und Herkunft aufbewahren. Neueste
verfügbare Definitionen einspielen und den konkreten Release-Kandidaten sowie
die entpackte Laufzeit prüfen. Scannerprotokolle und ihr tatsächliches Ergebnis
aufbewahren; ein lokaler sauberer Scan bestätigt keine globale Freigabe.
Die Windows-Produktressource erklärt den ursprünglichen Fund ebenfalls nicht.

Eine mutmaßliche Fehlklassifizierung gezielt über
[Microsofts Entwickler-Einreichung](https://www.microsoft.com/en-us/wdsi/filesubmission)
oder [Bitdefenders Meldeformular](https://www.bitdefender.com/consumer/support/answer/29358/)
prüfen lassen. Exakte Datei, Hash und reproduzierbaren Kontext zuordnen;
Herstellerantwort/Submission-ID separat festhalten. Neue Scans und
Herstellerkorrekturen anhand desselben Hashes verfolgen. Allgemeine Fundnamen
belegen allein keine Fehlklassifizierung; eine automatische Übernahme durch
andere Scanner ist nicht zugesichert. Uploads sind ein gesonderter,
ausdrücklicher Veröffentlichungsschritt; keine persönlichen Profile mitsenden.
Schutzfunktionen oder Ausnahmen sind kein Release-Fix.

## Auf GitHub einen Entwurf erzeugen

Nach dem ersten Push und einer Prüfung des gewünschten Commits:

```powershell
git tag -a v0.5.0 -m "üxzView 0.5.0"
git push origin v0.5.0
```

Dann **Actions → Prepare Windows release draft → Run workflow** auswählen und den vorhandenen Tag `v0.5.0` angeben (Versionsnummern im Beispiel an das tatsächliche Release anpassen). Tag, CMake-Version und ausgecheckter Commit müssen übereinstimmen. Der Workflow baut den Tag neu, prüft ihn und legt mit dem eingebauten `GITHUB_TOKEN` einen Release-Entwurf an. Ein zusätzliches persönliches Token ist nicht erforderlich. Nur dieser Job erhält `contents: write`; normale Build-Jobs erhalten `contents: read`. Ein Push oder Tag allein veröffentlicht nichts.

Den Entwurf unter Releases öffnen. Windows-ZIP frisch in ein leeres Verzeichnis entpacken und starten; Öffnen, Rahmenzoom, Pfeiltasten, Speichern, Vorschauen und Einstellungen sowie einen Batchlauf an Testkopien prüfen. Den tatsächlichen Testumfang im Text korrekt benennen, insbesondere Windows-10- und native Desktop-Tests. Alle fünf Download-Anhänge einschließlich Quellhinweisen und Prüfsummen beibehalten und erst dann **Publish release** wählen. Ein wiederholter Workflow für einen bereits vorhandenen Release schlägt fehl, statt dessen Dateien still zu ersetzen.

Alternativ lokal erstellte Dateien manuell unter Releases → Draft a new release hochladen: die beiden ZIPs, `SOURCES.md`, `dependency-sources.json`, `SHA256SUMS.txt`; den Inhalt von `release-notes.md` als Beschreibung einsetzen. Keine Linux-/macOS-Entwicklungspakete hinzufügen.

## Quellen dauerhaft verfügbar halten

Die DLL-Zuordnung vergleicht unveränderte Dateien mit dem SDK und prüft die SHA-256 der ursprünglichen Binärpakete. Für libheif/libavif werden zusätzlich Quellhashes, Ausgabehashes, Rezepte samt gemeinsamem Build-Helfer, Optionen und SDK-Lock des Neubaus geprüft. Die Indexprüfung bestätigt die Existenz passender Versionsnamen; sie ersetzt keine vollständige Inhaltsprüfung jedes Quellarchivs. Die Quellhinweise müssen direkt beim Binärdownload verfügbar bleiben. MSYS2 kann ältere Pakete entfernen: die referenzierten `.src.tar.zst` rechtzeitig spiegeln und die Downloadhinweise aktualisieren. Die Verantwortung für die Verfügbarkeit bleibt beim Distributor. Details: [THIRD_PARTY.md](THIRD_PARTY.md), [MSYS2-Quellserver](https://www.msys2.org/dev/mirrors/) und [GPLv3 Abschnitt 6](https://www.gnu.org/licenses/gpl-3.0.html#section6).

Der Ablauf entspricht dem dokumentierten [Erstellen von Release-Entwürfen mit GitHub CLI](https://cli.github.com/manual/gh_release_create). Hier lokal erzeugte Artefakte sind noch kein hochgeladenes oder veröffentlichtes GitHub-Release.

Beide Werkzeuge und alle fünf OCR-Sprachen liegen einsatzbereit unter `dist/uxzView/tools` und sind im Windows-ZIP enthalten. `dist/tools/` enthält zusätzlich die einzelnen `.ixtool`-Downloads zur erneuten Installation. Der Katalog `uxzView-tools.json` und `uxzView-tools-SHA256SUMS.txt` gehören dazu. Dies ist ein getrennt installierbares Funktionspaket, keine zweite Kopie der App. Der Release-Workflow hängt diese Dateien als zusätzliche Assets an den Entwurf. Für einen HTTPS-Katalog seine tatsächliche Adresse in der Erweiterungsverwaltung eintragen. Details und Quellen: [TOOL_EXTENSIONS.md](TOOL_EXTENSIONS.md).

Die Paketierung verwendet den hashgeprüften Werkzeug-Snapshot `build/bundled-tools`, nicht den persönlichen Zustand unter `dist/uxzView/tools`. Deployment erhält dortige Deaktivierungen, Entfernungen und zusätzliche Pakete. Auslieferungsbudget: 170 MB für den Betrachter, zusätzlich 25 MB für die enthaltenen Erkennungswerkzeuge und Sprachmodelle.
