# Architektur-, Ressourcen- und Zuverlässigkeitsprüfung · 28.10.2026

Historical 0.4.8 checkpoint. Version 0.5.0 subsequently removes the file journal and file-operation Undo; current behavior is documented in ARCHITECTURE.md and CHANGELOG.md.

Arbeitsstand: üxzView 0.4.8, einschließlich aller zuvor vorhandenen lokalen
Änderungen. Keine Commits, Pushes, Resets oder Bereinigungen persönlicher Daten.
Die beiden README-Bilder und ihre Einbindung bleiben erhalten. Der README-Text
zur Stapelverarbeitung nennt jetzt Berichte statt der bereits entfernten
Vorschauoberfläche.

## A. Gesamtzustand und Prüfrahmen

**Die Grundlage ist tragfähig; die Architektur ist noch nicht fertig entkoppelt.**
Core und Widgets sind getrennt, Originalpixel und Anzeigepixel bleiben getrennt,
Dateivorgänge besitzen Revisionsprüfungen und ein wiederherstellbares Journal.
Abbruch-/Generationsmechanismen, kontrollierte Testdaten und echte RAW-/OCR-/QR-
Prüfungen sind vorhanden. Das ist eine gute Basis für längerfristige Pflege.

MainWindow trägt weiterhin zu viel Koordination. Mehrere unabhängige
Speicherbudgets ergeben noch kein Gesamtbudget. Unterschiedliche Schreibwege
hatten unterschiedliche Schutzverträge. Diese Probleme sind wichtiger als
kosmetische Vereinheitlichung oder weitere abstrakte Schnittstellen.

Die Prüfung folgte tatsächlichen Datenflüssen und ihren Übergängen:

| Bereich | Betrachtete Implementierung und Vertrag | Urteil / Grenze |
| --- | --- | --- |
| Start/Build/Pakete | CMake, Presets, SDK-/Deploy-/Release-Skripte, Desktop-CI, Startup/Storage | Keine neuen Abhängigkeiten; native Bestätigung aller Zielsysteme bleibt offen. |
| Laden/Anzeige | ImageLoadRequest, CodecRegistry, Metadaten, Display-Konvertierung, ImageView | Originalpräzision bleibt maßgeblich; verwaistes Future-Ergebnis korrigiert. |
| Navigation/Indizierung | DocumentActivity, deferredOpen, DirectoryIndexService, Verzeichnis-Snapshots | Neueste wartende Anfrage und kooperativer Abbruch vorhanden; kein Umbau ohne reproduzierten Nutzen. |
| Vorschaubilder | PreviewService, Nachbarleiste, Galerie, Cache-Epochen | Begrenzte Worker/Queues; aktive Decoder liegen außerhalb der Cachebudgets. |
| Animation | APNG-Sitzung, Framecache, Wiedergabe/Seitenwechsel | Sitzung und ausgegebene Frames sind getrennte Ressourcen; Limits sind keine Prozessgrenze. |
| Bearbeitung/Verlauf | Undo/Redo, Auswahl, Quelle, Anzeigevorbereitung | Ownership und Budget jetzt in DocumentHistory; Übergänge isoliert prüfbar. |
| Schreiben/Dateien | normaler Export, verlustfreies JPEG, Extraktion, FileJournal/Recovery | JPEG-Revisionsschutz ergänzt; verbleibende Dateisystemrennen ausdrücklich benannt. |
| Batch | Planung, Ressourcen, maximal vier Worker, Fortschrittsmailbox, CSV | Bestehende Quell-/Zielprüfungen und Join-Verträge erhalten; Ergebnislebensdauer korrigiert. |
| Katalog/Erweiterungen | JSON/Locks, Metadatensuche/Duplikate, Formatmodule, ToolStore/ToolDialog | Kataloggröße bleibt Skalierungsrisiko; Tools besitzen Ausgabe-/Zeitgrenzen und Leases. |
| Vollbild/UI | ImageView, Native-Cover, Dialoge, Sprache/Design | Pixel-/Zustands- und native DPI-Prüfungen; kein neuer Compositor- oder GPU-Nachweis. |

Das ist keine formale Verifikation, vollständige Formatkonformitätsprüfung oder
Zeile-für-Zeile-Sicherheitszertifizierung sämtlicher Drittanbieterbibliotheken.

## B. Gefundene Probleme und Priorität

Kein neuer P0-Datenverlust wurde in dieser Prüfung reproduziert. Das belegt nicht
die Abwesenheit solcher Fehler. Folgende konkrete Probleme wurden behoben:

| Priorität | Datei / Funktion | Ursache und Auswirkung | Evidenz |
| --- | --- | --- | --- |
| P1 | `ui/main_window.cpp`, Worker-Abschluss | `result()` kopierte das Ergebnis, der dauerhafte Watcher behielt Original/Anzeige bis zum nächsten Auftrag. Paste/New/Delete konnten dadurch alte große Bilder halten. | Eigentümertest schlägt vor der Korrektur fehl; sechs Vorher-/Nachher-Speichermessungen je Arm. |
| P1 | `ui/main_window.*`, Zerstörung | Kein ausdrücklicher Join des eigenen Bildauftrags bei direkter/übergeordneter Zerstörung; ein eingereihter Auftrag konnte das Fenster überleben. | Mit Semaphore angehaltener Auftrag hält externe Testpixel nach alter Fensterzerstörung weiter fest. |
| P1 | `core/jpeg_transform.*`, `transformJpeg` | Verlustfreier Export hatte keine Quelle-/Zielrevisionsprüfung über Chooser, Scheduling und Verarbeitung. Externe Änderungen konnten unbemerkt verarbeitet oder überschrieben werden. | Implementierung vor Korrektur geprüft; neue Fälle schützen angelegtes/geändertes Ziel und geänderte Quelle. |
| P1 | `modules/archive_image.cpp`, ZIP-Vorprüfung | Begrenzte Dateianzahl begrenzte nicht die implizit erzeugten Elternverzeichnisse vor KZip. | 400 Dateien mit je 30 eigenen Eltern erzeugen 12.000 Verzeichnisse. Bisheriges Plugin akzeptiert die Datei, neue Regression weist sie ab. |
| P2 | `ui/main_window.cpp`, Verlauf | Nur Undo-Pixel wurden verrechnet. Redo, Bildtexte/ICC und native Metadaten lagen außerhalb dieses Budgets. Regeln und Quellenkorrekturen waren im Fenster verteilt. | Alter Codepfad geprüft; neue isolierte Tests für beide Richtungen, gemeinsame Metadaten, Eigenschaften und Budgetüberschreitung. |
| P2 | `ui/pipeline_dialog.cpp`, Abschluss | Dauerhaftes Future hielt abgeschlossene Batch-Listen auch nach Leeren des Ergebnismodells. | Derselbe nachgewiesene Future-Ownership-Vertrag; bestehende Batch-/Abbruchregressionen bleiben erhalten. |
| P2 | `ui/main_window.cpp`, Extraktionsspeichern | Zwischen Existenzprüfung und `save()` konnte ein Ziel entstehen; ein nicht explizit gebundener Save durfte den neuen Zustand als autorisiert übernehmen. | Technisch begründete Lücke; vorhandene Codec-Revisionsprüfungen werden durch explizit erwartete Abwesenheit genutzt. Kein Timingrennen als erfolgreicher Reproduktionstest ausgegeben. |
| P3 | Entwicklungs-/Funktionsdokumentation | Veraltete Testanzahl, Linux-Modulpfade und Beschreibung einer entfernten Batch-Vorschau. | Mit tatsächlichen Targets, Deploy-Code und Oberfläche abgeglichen und korrigiert. |

## C. Änderungen und ihre Grenzen

MainWindow und PipelineDialog konsumieren ihre einmaligen Ergebnisse mit
[`QFuture::takeResult()`](https://doc.qt.io/qt-6/qfuture.html#takeResult).
Das Ergebnis wird weiterverwendet, ohne eine zweite vollständige Pixelkopie zu
erzeugen. Ein konsumiertes Ergebnis wird nicht erneut gelesen. MainWindow trennt
bei Zerstörung die Completion-Verbindung und wartet vor dem Abbau seiner
Mitglieder auf den Auftrag. Das macht einen blockierenden Fremddecoder nicht
unterbrechbar und kann bei direkter Zerstörung entsprechend warten.

`core/document_history.*` besitzt jetzt beide Historienstapel, die Auswahl,
Übergänge und Quellenkorrekturen. Ein Ziel wird vor dem Worker kopiert, die
Transition erst nach erfolgreicher Anzeigevorbereitung bestätigt. Der Fehlerpfad
lässt die Historie unverändert. Datei-Undo und pixelbasiertes Undo bleiben getrennt.

Das gemeinsame Standardbudget beträgt 128 MiB und höchstens zwölf Einträge.
Bildschlüssel und Metadatenzeiger verhindern deren Mehrfachverrechnung innerhalb
des Verlaufs. `core/image_memory.*` liefert die auch vom Cache verwendeten Kosten
einschließlich Texte, ICC und nativer Metadaten. Kleine geteilte Strings können
konservativ mehrfach verrechnet werden. Bei Budgetdruck entfallen entferntere
Zustände; die zuletzt erzeugte inverse Operation bleibt erhalten. Ein einzelner
übergroßer Eintrag ist weiterhin erlaubt. Das ist eine begründete Änderung der
Verdrängung, keine neue harte RAM-Obergrenze und keine Änderung der Persistenz.

JPEG-Transformation akzeptiert optionale erwartete Quelle-/Zielrevisionen.
Alte Quellaufrufe bleiben gültig; die GUI reicht nun ihre beobachteten Revisionen
explizit hinein. Prüfung vor Verarbeitung und unmittelbar vor Veröffentlichung;
ein Fehler verwirft die temporäre Ausgabe. Extraktion verlangt explizit ein noch
nicht vorhandenes Ziel. QSaveFile plus Revisionen ist weiterhin kein atomarer
Dateisystem-Compare-and-swap.

Die ZIP-Vorprüfung begrenzt nun auch 10.000 implizite Verzeichnisse, bevor KZip
seinen Baum aufbaut. Das ergänzt die bestehenden Größen-, Datei- und Tiefenlimits.
Es findet weiterhin keine Extraktion auf die Festplatte statt.

Keine neue Laufzeitbibliothek, kein Hintergrunddienst, keine Änderung von
Profilformat, Speicherorten, Dateijournalformat oder Bildpräzision.

## D. Tatsächliche Verifikation

Windows 11, C++20, Release, Qt 6.11.2 und GCC 16.2:

| Prüfung | Ergebnis / Nachweis |
| --- | --- |
| Vollständiger CMake-Build | Bestanden; `build/architect-audit-build-final.log`. |
| Python | 45 Tests bestanden. |
| CTest | Sieben Suiten, 391 Qt-Fälle ohne Fehler/Skips: 171 Haupt-, 112 Workflow-, 43 Qualitäts-, 12 Storage-, 24 Journal-, 17 Erweiterungs- und 12 Maintenance-Fälle. |
| Gesamter lokaler Validator | Alle 29 Blöcke bestanden, einschließlich nativer Abläufe und 100/125/150/200 % DPI; `build/architect-audit-validation-summary.json`. |
| Fehler vor Korrektur | Zwei Lifetime-Fälle fehlgeschlagen: `architect-audit-repro-before.txt`; Archivfall mit bisherigem Plugin fehlgeschlagen: `architect-audit-archive-repro-before.txt`. |
| Korrigierte gezielte Prüfungen | Alle zwölf Maintenance-Fälle offscreen und nativ bestanden; abschließender Lauf nach den CLI-Messergänzungen ebenfalls bestanden. |
| Partielle statische Analyse | GCC `-fanalyzer`, drei Einheiten `document_history`, `image_memory`, `jpeg_transform`: Rückgabe 0, keine Warnungen; `build/architect-audit-analysis/summary.json`. |
| Wiederholte Nutzung | 30 kontrollierte 24-MP-Lade-/Ersetzungszyklen, Eigentümerfreigabe 30/30; `architect-audit-memory-30-cycles.json`. |
| Reduzierter Windows-Build | Frisch konfiguriert ohne Metadaten, Network und Archiv-Plugin; kompiliert, `--probe-image` liefert PNG 4 × 3/1 Seite mit Rückgabe 0; `architect-audit-reduced-{configure,build}.log` und `architect-audit-reduced-probe.json`. |
| Portable Ausgabe | Alle 391 Qt-Fälle ohne SDK-Pfade bestanden; echte QR-/OCR-Engines, fünf Sprachmodelle und umgezogene Profile/Tools bestanden. 136 PE-Module, keine fehlenden Imports; `architect-audit-portable.log`. |

Die portable Prüfung installiert ihre Testprogramme/QtTest nur vorübergehend und
entfernt sie wieder. Persönliche Profile/Tools werden bei Deployment nicht gelöscht.
Vorhandene Exiv2-/XMP-Warnungen der RAW-Fixtures bleiben in den Rohprotokollen;
die bestandenen Tests sind keine Behauptung fehlerfreier Metadaten aller Quellen.

Die Testanzahl zählt Qt-Setup/Cleanup und Datenzeilen mit. Wiederholte native/DPI-
Prüfungen sind keine zusätzlichen unabhängigen Funktionen. Dateiverändernde Tests
verwenden temporäre Verzeichnisse und selbst erzeugte Bilder. Benchmarks liefen
getrennt von Builds, Analyse und Desktoptests.

Nicht ausgeführt: native Windows-10-, Linux- und macOS-Läufe dieses Arbeitsstands,
ASan/TSan, GPU-/Compositor-Tracing, systemweites I/O-Profiling, echte Netzlaufwerke,
Stromausfalltests und stundenlange gemischte Nutzung. Der partielle GCC-Analyzer
ersetzt diese Prüfungen nicht. Der Source-/CI-Aufbau ist vorbereitet; neue native
GitHub-Läufe sind weiterhin nötig.

## E. Vorher / Nachher

Kontrollierte 6000 × 4000 RGB-PNG-Datei: 96.000.000 Pixelbytes. Verstecktes Fenster,
Dokument-/Thumbnailcache deaktiviert, danach Ersetzen durch ein 3 × 2-Bild aus der
Zwischenablage. Zwei frische Prozesse je Arm, je drei Durchgänge; Reihenfolge
Vorher/Nachher/Vorher/Nachher. Die erste Vorher-Baseline wurde vor der Korrektur
erfasst. Betriebssystem-Dateicaches wurden nicht geleert.

| Messung | Vorher | Nachher |
| --- | ---: | ---: |
| Alte Pixel nach Ersetzen nur noch vom Testbeobachter gehalten | 0/6 | 6/6 |
| Privater Prozessspeicher nach Ersetzen, Median | 131,06 MiB | 39,49 MiB |
| Innerhalb eines Durchgangs freigegebener privater Speicher, Median | 0,00 MiB | 91,74 MiB |

Windows-Prozesszähler umfassen auch Heap-/Pluginzustände und sind keine exakte
Bilanz lebender QImage-Puffer. Der Eigentümertest belegt den Zusammenhang
unabhängig vom Rückgabeverhalten des Allocators. Im zusätzlichen 30-Zyklen-Lauf
liegt der private Speicher nach Ersetzen zwischen 39,13 und 39,76 MiB, am Ende bei
39,76 MiB; alle 30 Pixelpuffer werden freigegeben. Das ist ein begrenzter
Ressourcentest, kein Beweis gegen jedes Langzeit-Leak.

Kontrollmessungen des bestehenden Benchmarks, zwei frische Prozesse je Arm:

| Warmes Dateisystem, identischer Pfad | Vorher Median (Bereich) | Nachher Median (Bereich) |
| --- | ---: | ---: |
| JPEG 24 MP, Registry-Decode, 10 Proben | 372,42 ms (290–665) | 320,93 ms (289–518) |
| PNG 24 MP, Registry-Decode, 10 Proben | 437,80 ms (333–527) | 346,70 ms (294–500) |
| 10.000-Datei-Index, 2 Proben | 56,33 ms (25–87) | 24,30 ms (23–26) |
| Leeres Offscreen-Fenster, 10 Proben | 18,74 ms (11–61) | 10,07 ms (9–46) |

**Diese niedrigeren Kontrollmediane belegen keinen allgemeinen Geschwindigkeits-
gewinn.** Decoder und Indexalgorithmus wurden hier nicht beschleunigt; die kleine
Stichprobe und warmen OS-/Plugin-/Heapzustände schwanken. Cache-Miss, spätere
Cachetreffer und erste Prozessinitialisierung sind in den Rohdaten unterscheidbar.
Ein echter kalter Datenträgerstart wurde nicht gemessen. Die belastbare Verbesserung
dieses Durchlaufs ist die Freigabe unnötig gehaltener Bildressourcen.

Rohdaten: `build/architect-audit-comparison.json`,
`architect-audit-memory-{before,after}-{1,2}.json`,
`architect-audit-general-before.json`, `architect-audit-general-before-2.json`,
`architect-audit-general-after-{1,2}.json` und die jeweils aufbewahrten Executables.
Rendering-Kontrollberichte liegen unter `architect-audit-render-{before,after}.json`;
sie sind weder native FPS-Messungen noch ein Beleg schnellerer Bilddekodierung.
Die Mediane vollständiger gecachter Repaints liegen als Kontrolle bei 0,519 →
0,502 ms (8 Bit) und 0,497 → 0,491 ms (16 Bit). Die Bildansicht wurde nicht
optimiert; diese Werte dienen der Einordnung, nicht einem Wirkungsversprechen.

## F. Verbleibende Risiken und nächste Prioritäten

**P1 – Gesamt-RAM unter gleichzeitiger Last.** Die nominalen Standardbudgets für
Dokumentcache (192 MiB), Thumbnailcache (16), View-Raster (bis 32 pro Ansicht),
APNG-Sitzung (64) und Historie (128 pro Fenster) summieren sich für ein Fenster
auf 432 MiB. Geteilte Puffer dürfen dabei nicht einfach als tatsächlicher RAM
addiert werden. Original/Display, aktive Decoder, Undo-Ausnahme, temporäre Filter-
und Encoderpuffer und parallele Batch-Dateien kommen zusätzlich hinzu. Ein
512-MiB-Einzelbildlimit begrenzt weder einen Transformationspeak noch vier Worker.
Mehrere Fenster/PreviewServices vervielfachen aktive Arbeit. Als Nächstes reale
gleichzeitige RAW-/Edit-/Batch-Last messen; erst danach gemeinsame Zulassung oder
adaptive Parallelität einführen, statt einen neuen Cache-Manager zu erfinden.

**P2 – MainWindow-Koordination.** Loading/Navigation, Save-Dialogtransaktionen und
Dateiverwaltungspräsentation bleiben gekoppelt. DocumentActivity ist weiterhin
GUI-thread-exklusiv, aber viele Übergänge sind implizit. Eine nächste Extraktion
muss eigene Lebensdauer und Ein-/Ausgaben besitzen; bloß weitere `.cpp`-Dateien
oder generische Controller-Interfaces verbessern das nicht.

**P2 – Weitere Schreib-/Lesegrenzen.** Die JPEG-Lücke ist geschlossen, normale
Saves/Batch behalten ihre vorhandenen Prüfungen. Mehrseitige Extraktion ist
weiterhin eine Folge einzelner Exporte, keine atomare Gesamttransaktion; die
Kohärenz über mehrere Seiten bei externer Quelländerung braucht einen gesonderten
Regressionstest und einen übergreifenden Quell-Snapshot. Dateisystemprüfungen und
QSaveFile-Commit haben grundsätzlich ein verbleibendes externes Rennfenster.

**P2 – Große Kataloge und Journalansichten.** Ganze JSON-Snapshots/Journalindizes
werden weiterhin gelesen; Katalogschreiben und Metadatenbibliothek haben bewusst
grobe Locks. Vor einer Datenbankmigration große reale Datenbestände und die
GUI-Pausen messen. Migration, Backup und Korruptionsverhalten gehören zum Entwurf.

**P2 – Blockierende Fremdarbeit und Plattformen.** Abbruch unterbricht keinen
beliebigen OS-/Decoderaufruf. Join schützt Eigentümer, garantiert aber keine kurze
Zerstörungszeit auf kaputten/entfernten Laufwerken. Windows 10, beide Macs und
Linux müssen den aktuellen Stand nativ bestätigen; RAW-/Farbvarianten bleiben
breiter als die vorhandenen Fixtures.

**P3 – APNG-/Profiling-Kandidaten.** Bitweise CRC-Berechnung und zusammengesetzte
Chunkpuffer sind bei großen APNGs ein technisch plausibler CPU-/Allokationskandidat.
Ohne einen passenden Vorher-/Nachher-Workload wurde daran bewusst nichts geändert.
Kein Versprechen über Wirkung oder Priorität gegenüber echten Decodezeiten.

## G. Wartbarkeitsurteil

Ein neuer Entwickler kann Codec-/Cache-, Index-, Journal-, Batch- und jetzt
Historienlogik unabhängig von einem Hauptfenster verstehen und testen. Die neue
History-API besitzt reale Zuständigkeit und Invarianten; sie verschiebt nicht nur
Fenstermethoden. Gemeinsame Kostenfunktionen vermeiden auseinanderlaufende
Metadatenbudgets. Herkunftshinweise, Privacy-Regeln und plattformbezogene Build-
Anleitungen bleiben Teil des Projekts.

Für Änderungen an Hauptfenster-Koordination und nativer Vollbilddarstellung ist
weiterhin ein genauer Blick auf Signalreihenfolge, modale Schleifen und die
Regressionssuiten nötig. Das ist die größte verbleibende Wartungslast. Der Stand
ist nachweislich besser begrenzt und einfacher isoliert prüfbar, aber kein
wartungsfreies System und keine abgeschlossene Fünfjahres-Architektur.
