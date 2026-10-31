# Gegenprüfung der externen Kritik · 31.10.2026

Geprüft wurde der aktuelle Entwicklungsstand von üxzView 0.5.0 anhand von
Quellcode, vorhandenen Prüfergebnissen und einem zusätzlichen Großbildversuch.
Die Versionsnummer allein identifiziert keinen exakten Quellstand. Die
Sternebewertungen und „geplanten Lösungen“ der zugelieferten Mistral-Liste sind
keine beschlossene Projekt-Roadmap.

Die Kritik benennt echte Grenzen: Es fehlt ein gemeinsames RAM-Budget, die
Bildpipeline arbeitet ohne Kachelung, und native Desktop-/Farbprüfungen sind
unvollständig. Mehrere zentrale Behauptungen sind dagegen falsch oder vermischen
fehlende Validierung mit fehlender Implementierung. Eine mögliche Störung ist
ohne reproduzierende Datei, Konfiguration und Messung noch kein bestätigter Bug.

## Alle 35 Aussagen

| Nr. | Aussage | Gegenprüfung und Beleg |
| --- | --- | --- |
| 1 | Wayland-Screenshots fehlen | **Zutreffende Funktionslücke.** Die globale Aufnahme verwendet `QScreen::grabWindow(0)` und meldet die fehlende Portal-Anbindung ausdrücklich. Globale Screenshot-Hotkeys sind derzeit Windows-spezifisch. Siehe `src/ui/main_window.cpp`, `src/ui/platform_support.cpp`. |
| 2 | Mehrmonitor-Verhalten ungetestet | **Offene native Validierung.** Monitorauswahl und Vollbild-Zuordnung sind implementiert; strukturelle Tests ersetzen keine physischen Mehrmonitorprüfungen. Siehe Vollbildpfad in `main_window.cpp`, [Qualitätsgrenzen](QUALITY.md). |
| 3 | HDR-Desktop ungetestet | **Zutreffende Prüfgrenze.** Float-/16-Bit-Leseprüfungen und ICC-Verarbeitung bestehen, garantieren aber keine kalibrierte HDR-Ausgabe. `displayImage()` in `src/core/image_loading.cpp` konvertiert gültige Nicht-sRGB-Profile nach sRGB. HDR-Monitorbetrieb bleibt separat zu prüfen. |
| 4 | Linux-/macOS-DPI unvalidiert | **Offene native Validierung.** Qt-HiDPI ist vorhanden; die lokale Vier-Skalierungs-Matrix betrifft Windows. Ein konkreter verpixelter Linux-/macOS-Dialog wurde nicht belegt. |
| 5 | Kein Gigapixel-Decoder | **Zutreffende Architekturgrenze.** Ganze dekodierte Bilder liegen als `QImage` vor. Die daraus abgeleitete 96-MiB-Ladegrenze stimmt jedoch nicht, siehe Versuch unten. |
| 6 | Einzelbilder über 96 MiB nicht lad-/bearbeitbar | **Falsch.** 96 MiB begrenzen die Aufnahme in den zusätzlichen Dokumentcache, nicht das zurückgegebene Bild. Ein Bild mit 100 MiB Pixeln wurde über `MainWindow` geladen, freigestellt und per Undo wiederhergestellt. Siehe `src/core/preview_service.cpp` und Versuch unten. |
| 7 | Keine Kachelung | **Zutreffend.** Es gibt keine gekachelte, außerhalb des RAM arbeitende Pixelpipeline. Eine Anzeigevorschau beseitigt diese Grenze für vollständige Bearbeitung nicht. |
| 8 | Kein globales RAM-Budget | **Zutreffend und wesentlich.** Aktives Dokument, Decoder, Bearbeitung, Anzeige, Undo und parallele Batchaufträge besitzen keine gemeinsame Reservierungsinstanz. Einzelgrenzen und Cachebudgets ergeben keine Prozessobergrenze. Siehe [Speicherbericht](MEMORY_REVIEW_2026-10-29.md). |
| 9 | Keine Speicherwarnung | **Teilweise.** Größen-/Allokationsgrenzen und Fehlerausgaben existieren; dynamische Betriebssystem-Druckerkennung fehlt. Vorrangig sind Reservierung, Freigabe und begrenzte Parallelität sinnvoll, statt erst bei Speichermangel zusätzliche Dialoge zu öffnen. Siehe `image_document.cpp`, `processing.cpp`, `codec_registry.cpp`. |
| 10 | Keine atomaren Dateioperationen | **Als Pauschalaussage falsch.** Bildexport und JPEG-Transformation verwenden `QSaveFile` mit Revision-/Inhaltsprüfungen. Atomarer Einzeldateiersatz ist keine Sperre gegen andere Prozesse, keine Mehrdatei-Transaktion und keine universelle Stromausfallgarantie. Zwischen letzter Prüfung und Commit bleibt ein Konkurrenzfenster. Siehe `codec_registry.cpp`, `jpeg_transform.cpp`, `file_operations.cpp`. |
| 11 | MakerNotes unvollständig | **Eingeschränkte Kameraabdeckung zutreffend.** Die dokumentierte Fixture-Warnung betrifft Nikon; eine allgemeine Canon-Fehlfunktion ist nicht belegt. Exiv2-Unterstützung und reale Kameradateien müssen gemeinsam geprüft werden. Siehe [Formatprüfung](FORMAT_COMPATIBILITY.md), [Qualität](QUALITY.md). |
| 12 | Kein HDR-Metadateneditor | **Zutreffende Funktionslücke.** Daraus folgt kein belegter Fehler beim normalen Anzeigen. Ein spezialisierter Editor gehört nicht automatisch zum vereinbarten Kernumfang. |
| 13 | Verlustfreies JPEG bei EXIF-Ausrichtung ungleich 1 gesperrt | **Zutreffende Schutzmaßnahme.** `jpeg_transform.cpp` und der UI-Pfad verhindern derzeit diese Transformation. Normale Anzeige berücksichtigt die Ausrichtung. Pixel drehen und erneut JPEG speichern wäre nicht verlustfrei; Koeffiziententransformation, orientierte Koordinaten, MCU-Grenzen und Metadaten müssen zusammen gelöst werden. |
| 14 | Kein PDF | **Zutreffende Funktionslücke.** Ein optionales Backend wäre eine Erweiterung mit eigenen Größen-, Lizenz- und Deploymentkosten. |
| 15 | Kein CBR/RAR | **Zutreffend.** ZIP/CBZ ist dagegen implementiert und geprüft. Siehe `src/modules/archive_image.cpp`, [Formate](FORMAT_COMPATIBILITY.md). |
| 16 | Kein JPEG XR | **Zutreffend.** JPEG XL ist ein anderes Format und bereits vorhanden. |
| 17 | ICNS-Schreiben gesperrt | **Zutreffende Schutzmaßnahme.** Der angebotene Encoder änderte in Prüfungen die Bilddimensionen. Die Sperre verhindert diese Ausgabe, statt Nutzern bekannte stille Größenänderungen zuzumuten. Siehe [Formatprüfung](FORMAT_COMPATIBILITY.md). |
| 18 | HEIF/HEIC eingeschränkt, HDR könnte fehlschlagen | **Abdeckung begrenzt.** RGB-/Alpha-Roundtrips sind geprüft; die breite HDR-Variantenabdeckung fehlt. Die Vermutung ist kein reproduzierter HDR-Defekt und rechtfertigt allein kein blindes Bibliotheksupgrade. |
| 19 | AVIF 12-Bit/HDR könnte fehlschlagen | **Offene Variantenprüfung.** Normale AVIF-Unterstützung existiert. Es fehlt der Nachweis für die genannten HDR-/12-Bit-Varianten, sowohl für Erfolg als auch für einen konkreten Fehler. |
| 20 | Keine Animations-/Mehrseitenencoder | **Zutreffende Exportgrenze.** Der aktuelle Frame beziehungsweise die aktuelle Seite kann exportiert werden; vor Reduktion auf ein Einzelbild wird gewarnt. Vollständige Animation-/Mehrseitenausgabe fehlt. |
| 21 | Keine parallelen/asynchronen Encoder | **Falsch.** Einzelbildspeichern läuft über `QtConcurrent`/`QFutureWatcher` im Worker, Batch über ein bis vier Threads. Codecinterne Parallelisierung und unterbrechbare Encoder sind davon zu unterscheiden. Siehe `src/ui/main_window_files.cpp`, `src/core/batch_pipeline.cpp`. |
| 22 | Keine Fortschrittsanzeige pro Datei | **Teilweise.** Gesamtfortschritt und Ergebnis-/Fehlerzeilen je Datei existieren. Kontinuierlicher Prozentfortschritt innerhalb eines einzelnen Codecaufrufs fehlt; ohne Codec-Rückmeldung wäre eine solche Prozentzahl erfunden. |
| 23 | Kein Datei-/Ordner-Drag-and-drop | **Teilweise falsch.** `setAcceptDrops`, `dragEnterEvent` und `dropEvent` öffnen die erste lokale Dateiadresse. Ein eigener Ordner-Drop-Ablauf fehlt: Ein Ordner wird nicht zur ersten Bilddatei aufgelöst. Native Wayland-Drops sind noch zu prüfen. |
| 24 | Kein Drucken | **Falsch.** `QPrinter`, `QPrintDialog`, `QPainter` und Seitenanpassung sind im `print`-Befehl implementiert; Qt PrintSupport wird gelinkt. Echte Drucker-/Farbqualität wurde damit noch nicht validiert. Siehe `main_window.cpp`, `CMakeLists.txt`. |
| 25 | Linux-/macOS-Tools nur manuell | **Teilweise.** Fertige Engines und fünf OCR-Sprachen liegen aktuell dem Windows-Paket bei. `scripts/build_tool_packs.py` enthält native Builds für Linux/macOS; passende lokale/Katalog-Pakete kann der Manager installieren. Fertige dort nativ validierte Pakete fehlen weiterhin. |
| 26 | Keine GPU für OCR/QR | **Zutreffend für die gelieferten CPU-Tools.** Ein GPU-Pfad ist nicht integriert. Dass CUDA/OpenCL diese konkreten Aufgaben einschließlich Start-/Transferkosten beschleunigen würde, ist nicht gemessen. |
| 27 | Erste Bildanzeige durch 10.000er-Index 1–2 s langsam | **Nicht belegt; Ursache so nicht aus dem Code ableitbar.** Das Dokument wird installiert, bevor die asynchrone Ordnerindexierung fertig ist. `DirectoryIndexService` besitzt einen Worker. Die historische 10.000er-Messung von 6570,28 ms betrifft Hintergrundindexierung, nicht Erstbildlatenz. Konkrete langsame Fälle benötigen getrennte Messungen. |
| 28 | RAW 500–1000 ms, Vorschaucache fehlt | **Zeitangabe ohne Fixture/Hardware nicht belastbar; Cachevorschlag bereits teilweise umgesetzt.** Dokumentcache wird vor eingebetteter RAW-Vorschau geprüft; bei Miss folgen Vorschau und vollständiges Decoding. Miniaturcache existiert ebenfalls. Volles RAW-Decoding bleibt aufwendig. Siehe `image_loading.cpp`, `preview_service.cpp`. |
| 29 | Keine GPU-Bildbearbeitung | **Überwiegend CPU-Pipeline zutreffend.** Ein allgemeiner Geschwindigkeitsfehler ist damit nicht bewiesen. Upload/Readback, Präzision, kleine Bilder und plattformabhängige Treiber müssen in einem End-to-end-Vergleich berücksichtigt werden. |
| 30 | Kein Memory-Mapping für große Bilder | **Kein eigener Out-of-core-Pixelpfad vorhanden.** Das Mappen komprimierter JPEG-/PNG-Dateibytes ersetzt keine gekachelte Dekodierung. Die Exiv2-`mmap()`-Schnittstelle in `metadata.cpp` nutzt einen begrenzten eigenen Snapshot und ist keine Gigapixel-Lösung; Drittanbieter-internes Mapping wurde nicht allgemein ausgeschlossen. |
| 31 | MainWindow zu groß | **Wartbarkeitsschuld zutreffend.** Aktuell 2286 Zeilen in `main_window.cpp` und 261 in `main_window_files.cpp`; die Klasse orchestriert weiterhin zu viele Zustände. `DocumentHistory`, `DirectoryIndexService`, `SlideshowController` und `FileOperations` sind bereits getrennt. Weitere Verantwortlichkeiten müssen mit klaren Zustandsgrenzen ausgelagert werden; bloßes Dateiaufteilen reicht nicht. |
| 32 | Keine vollständigen Linux-/macOS-Tests | **Native Desktopabdeckung offen; CI keineswegs abwesend.** `desktop.yml` baut Windows x64, Linux x64 und beide macOS-Architekturen und führt Tests, Installationsprobe und Paketierung aus. Ein zusätzlicher Windows-10-Self-hosted-Workflow existiert. Hier wurden deren aktuelle GitHub-Ergebnisse nicht neu verifiziert; lokale Windows-11-Erfolge garantieren keine anderen Desktops. |
| 33 | Keine statische Analyse | **Teilweise falsch.** Dokumentierte GCC-`-fanalyzer`-Prüfungen betreffen unter anderem `document_history`, `image_memory` und `jpeg_transform`. Ein kontinuierlicher projektweiter Clang-Tidy-/Sanitizer-Lauf fehlt. Siehe [Architekturprüfung](ARCHITECTURE_REVIEW_2026-10-28.md). |
| 34 | Keine Fuzz-Tests | **Zutreffend für dedizierte Fuzz-Harnesses.** Fehlerhafte, abgeschnittene und Traversal-Testdateien existieren, ersetzen aber kein Coverage-gesteuertes Fuzzing. Eigene APNG-/Archivgrenzen sind sinnvolle erste Ziele. |
| 35 | Keine Plugin-API-Dokumentation | **Falsch als Pauschalaussage.** [Plugin-Manager](PLUGIN_MANAGER.md) dokumentiert Manifest, Qt-ImageIO-Pakete, ABI/OS/Architektur, Hashes und Grenzen; [Funktionserweiterungen](TOOL_EXTENSIONS.md) dokumentieren `.ixtool`, Ein-/Ausgabeprotokoll und Laufzeitlimits. Eine stabile öffentliche C++-ABI für interne Module wird nicht versprochen. Nicht implementierte PDF-/KI-Schnittstellen sind davon zu unterscheiden. |

## Nachweis: 100 MiB Pixel sind lad- und bearbeitbar

Der neue optionale Modus in `tests/bench_ixview.cpp` erzeugt eine temporäre
5120 × 5120 große RGB32-BMP: **104.857.600 Byte dekodierte Pixel (100 MiB)**,
78.643.254 Byte Dateigröße. Die Fixture und die Einstellungen liegen isoliert;
der Erzeugerpuffer wird vor dem Decoding freigegeben.

```text
build/ixview_benchmarks.exe build/external-review-cache-admission.json --profile-cache-admission -platform offscreen
```

Ergebnis unter Windows 11 mit Qt 6.11.2:

- Zwei Dokumentcache-Lesevorgänge liefern das Bild erfolgreich und decodieren
  zweimal. Der 96-MiB-Aufnahmeschwellenwert verhindert nur die Cacheaufnahme.
- `MainWindow` lädt das Bild, schneidet eine 120 × 90-Auswahl aus und stellt mit
  Pixel-Undo die ursprünglichen 5120 × 5120 wieder her.
- Der SHA-256 der Quelldatei bleibt unverändert.

Das ist ein ausgeführter Offscreen-Test der Lade-/Bearbeitungspipeline, keine
visuelle Desktopprüfung, Peak-RAM-Messung oder Gigapixel-Zusage. Animationen und
RAW-Varianten besitzen eigene Grenzen und sind durch diesen BMP-Versuch nicht
abgedeckt. Ergebnisdatei: `build/external-review-cache-admission.json`.

Die tatsächliche Sicherheitsgrenze steht in `ImageDocument::safeSize`: maximal
100.000 je Dimension und ein 512-MiB-Budget, standardmäßig konservativ mit acht
Byte je Pixel gerechnet, also 67.108.864 Pixel. Nachprüfungen berücksichtigen
das tatsächliche Format. Das ist weder die Dateigröße auf Datenträger noch eine
globale Prozessgrenze. Auch der Qt-Leser besitzt eine Allokationsgrenze.

## Atomizität und Wayland richtig abgrenzen

`QSaveFile` schreibt in eine temporäre Datei und veröffentlicht per atomarem
Ersatz, wenn der verwendete Dateisystempfad das unterstützt. Diese Eigenschaft
schützt vor einem nur teilweise veröffentlichten Einzelbild, nicht vor beliebigen
gleichzeitigen Änderungen anderer Prozesse. Die Behauptung, QSaveFile biete
grundsätzlich keine echte Dateisystem-Atomizität, widerspricht seiner
[Qt-Dokumentation](https://doc.qt.io/qt-6.8/qsavefile.html).

Wayland-Bildschirmaufnahme benötigt eine eigene, vom Benutzer freigegebene
[Screenshot-Portal-Anfrage](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Screenshot.html).
Das Fehlen dieser Anfrage beweist keine fehlende gewöhnliche Monitor-, HiDPI-
oder Drag-and-drop-Unterstützung. Diese läuft über Qt und dessen
[Wayland-Plattformintegration](https://doc.qt.io/qt-6.8/qtwaylandclient-index.html);
die CI fordert QtWayland bereits an. Die im Vorschlag genannte Methode
`QGuiApplication::setPlatformPluginPath()` ist keine vorhandene öffentliche
Qt-API. Ein globaler Pluginpfad-Hack oder erzwungenes XWayland ist keine
begründete Lösung für diese getrennten Aufgaben.

## Tatsächlicher Fehler bei dieser Prüfung

Die Batchoberfläche versprach noch **„Ersetzen mit Sicherung“**, obwohl das
Entfernen von Dateijournal und Datei-Undo auch die dauerhaften
Ersetzungssicherungen entfernt hatte. Die Beschriftungen heißen jetzt
**„Vorhandene Dateien ersetzen“** und **„Ersetzen“**, in allen fünf Sprachen.
Eine ungenutzte alte Journal-/Undo-Übersetzung wurde entfernt. Der atomare
Speicherpfad bleibt bestehen; eine private Sicherung oder ein Datei-Undo wird
nicht mehr zugesagt. Auch die Architekturtexte grenzen die Cacheaufnahme nun
deutlich von der Ladegrenze ab.

## Sinnvolle Reihenfolge

1. **Speicherverlässlichkeit:** gemeinsame Reservierungen für große Decoder,
   Bearbeitung und Batch entwerfen; Peak-Speicher mit repräsentativen Dateien
   messen. Bestehende Grenzen bis zu einem belegten Ersatz beibehalten.
2. **Plattformnachweise:** Windows 10, Linux/Wayland und macOS nativ prüfen,
   einschließlich DPI, physischer Monitore, Farbmanagement und Vollbildwechseln.
3. **Wartbarkeit und Eingabegrenzen:** Laden/Speichern/Navigation/Aufnahme mit
   klarer Zustandsverantwortung schrittweise aus `MainWindow` lösen;
   Sanitizer-/Fuzz-Harnesses für eigene Parser ergänzen.
4. **Gezielte Funktionen:** Screenshot-Portal, Ordner-Drop und verlustfreie
   orientierte JPEG-Transformation jeweils separat spezifizieren und prüfen.
5. **Bedarfsabhängiger Ausbau:** PDF/CBR/JPEG XR, Kachelung und GPU-/HDR-Pfade
   anhand echter Anwendungsfälle und End-to-end-Messungen bewerten.

Das ist eine Bewertung der nächsten Kandidaten, keine Zusage, alle Funktionen
in die Standardinstallation aufzunehmen. Insbesondere macht Memory-Mapping
allein aus dem bestehenden Editor keinen speichersparenden Gigapixel-Editor.

## Verifikation und Grenzen

Der Entwicklungsbuild und das aktualisierte Windows-Paket bestehen jeweils alle
**465 Qt-Fälle** der acht Suiten; alle **45 Python-Tests** bestehen ebenfalls.
Der neue Großbildnachweis ist ein eigener Benchmarkmodus und erhöht diese
Fallzahl nicht. Die Portable-Prüfung umfasst außerdem OCR in fünf Sprachen,
QR-Erkennung und einen verlegten Portabelordner ohne SDK im Suchpfad.

Ein erster Portable-Lauf scheiterte in zwölf Archivfällen bereits am Erstellen
der Fixtures im abgeschotteten System-Tempordner. Mit projektlokalem Tempordner
bestanden diese Fälle; der nächste Sandbox-Lauf blockierte das Erstellen einer
temporären Windows-Junction im Erweiterungs-Sicherheitstest. Der vollständige
Lauf wurde deshalb mit projektlokalen Fixtures außerhalb der Sandbox wiederholt;
dafür wurde kein Produktcode geändert. Die ursprünglichen Fehler bleiben in
`build/external-review-portable-system-temp.log` und
`build/external-review-portable-sandbox-junction.log` sowie den zugehörigen
`external-review-system-temp-functional.txt`- und
`external-review-sandbox-extension.txt`-Berichten erhalten.

Lokale Belege: `build/external-review-build.log`,
`build/external-review-validation-summary.json`,
`build/external-review-cache-admission.json`,
`build/external-review-portable.log`. Die frühere vollständige
33-Block-Oberflächenmatrix wurde für diese Textkorrektur nicht wiederholt.
Native Windows-10-/Linux-/macOS-Desktopprüfungen und belastbare
Monitor-/HDR-/RAW-Variantenprüfungen bleiben offen. Es wird kein neuer
allgemeiner Geschwindigkeitsgewinn behauptet.
