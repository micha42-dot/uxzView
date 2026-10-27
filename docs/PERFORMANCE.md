# Performance in üxzView 0.4

Messung mit `ixview_benchmarks`, Windows 11, Release-Build, Qt 6.11.2 und Offscreen-Oberfläche. Rohdaten: `tests/output/performance-before.json` und `performance-after.json`. Kein synthetischer Durchlauf ersetzt Messungen mit echten Kameradateien, Netzlaufwerken, vielen Metadaten oder der tatsächlichen Desktopausgabe.

## Änderungen

- Ordnersuche, natürliche Sortierung und Aufbau der Positions-Hashtabelle laufen im Worker. Sortierschlüssel und benötigte Dateiattribute werden einmal pro Eintrag vorbereitet. Änderungszeiten werden bei normaler Namenssortierung nur für den Hotfolder eingelesen. Die Liste bleibt beim Blättern innerhalb desselben Ordners erhalten. Dateisystemereignisse werden gebündelt und führen zu einer erneuten Hintergrundsuche. Tastendrücke bleiben bis zur Übernahme einer fertigen Liste vorgemerkt.
- Die normale Liste der Dateiendungen wird aus Qt-Pluginmetadaten gelesen, ohne hierfür alle Decoder und deren Abhängigkeiten zu instanziieren. Vollständige Codec-Fähigkeiten werden erst bei Bedarf abgefragt und im Prozess zwischengespeichert.
- Optionale Nachbarvorschauen sind standardmäßig ausgeschaltet und werden erst beim Aktivieren angelegt. Maximal zehn Nachbarn, zwei niedrig priorisierte Vorschau-Worker, 16 MiB globaler Vorschaucache. Bildverwaltung fordert nur sichtbare Miniaturen an. Veraltete Anfragen werden verworfen; ein laufender Decoder kann nicht mittendrin unterbrochen werden.
- Ein eigener 192-MiB-Cache hält unveränderte dekodierte Dokumente bis 96 MiB pro Bild. Schlüssel berücksichtigen Pfad, Dateigröße, Änderungszeit und Seite. Copy-on-write verhindert, dass eine Bearbeitung den Cache verändert. F5 leert die Bildcaches und liest die Ordnerliste neu. Bilder über 96 MiB werden bewusst nicht in diesen Zusatzcache aufgenommen.
- RAW-Miniaturen verwenden das bei Bedarf geladene LibRaw-Modul und eingebettete Kameravorschauen. Ohne eingebettete Vorschau wird für eine Miniatur keine vollständige RAW-Entwicklung gestartet. Beim eigentlichen Öffnen läuft die vollständige Entwicklung weiter; eine schnelle Vorschau wird nicht als bearbeitbares Original übernommen. Seit 0.4.3 sind fünf reale CR2-/NEF-/ARW-/PEF-/DNG-Fixtures einschließlich eingebetteter Vorschauen geprüft; weitere Varianten bleiben offen.
- Der Erweiterungsmanager und Qt Network liegen in einem eigenen Modul. Der Test prüft unter Windows, dass Qt6Network vor dem Öffnen des Managers noch nicht geladen ist. TLS-Backends werden mit dem portablen Paket ausgeliefert und ihre Verfügbarkeit geprüft.
- Metadatenindex und Duplikatsuche starten nur auf Wunsch. Der Index liest vorhandene Katalogdaten einmal pro Lauf und speichert nur veränderte Metadaten. Parallele Metadatenzugriffe werden wegen des gemeinsamen XMP-Zustands von Exiv2 serialisiert; die Bilddecoder arbeiten weiter parallel.
- Batch hat einstellbar 1–4 parallele Dateien, standardmäßig 2. Jede Datei wird zunächst separat geschrieben und erst nach erfolgreichem Export ins Ziel übernommen. Die maximale Dateizahl pro Worker, 512-MiB-Bildgrenzen und Cachebudgets ersetzen keine globale RAM-Grenze: große Bilder, Anzeige, Undo, Encoder und parallele Dateien können zusammen deutlich mehr Speicher benötigen.

## Reproduzieren

```powershell
$env:PATH="$PWD/.deps/sdk/ucrt64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
./build/ixview_benchmarks.exe tests/output/performance-after.json
```

Der Benchmark erzeugt unter `build/perf-fixtures` zwei deterministische 24-Megapixel-Bilder, eine 2-Megapixel-Cachedatei und einen Ordner mit 10.000 kleinen Dateien. Die Fixtures werden nicht mit dem Quellpaket ausgeliefert. Decoder und Fenster werden fünfmal gemessen. Die Zeiten sind Momentaufnahmen mit bereits warmem Dateisystemcache und schwanken mit der Rechnerlast. Vorher-/Nachher-Dekodierwerte sind deshalb keine belastbare allgemeine Beschleunigungsquote.

Der Positionsvergleich führt im selben Build 10.000 Zugriffe auf dieselbe Liste einmal mit linearer Suche und einmal mit der Hashtabelle aus. Das trennt die Datenstrukturwirkung von wechselnder Rechnerlast. Die 10.000 Dateien sind keine repräsentative RAW-/Metadatensammlung und messen keine vollständige Miniaturansicht.

## Grenzen

Kein gekachelter Gigapixel-Decoder, keine unterbrechbaren Qt-Decoder, kein vorauseilendes vollständiges RAW-Decoding. Der Metadatenkatalog bleibt eine atomar geschriebene JSON-Datei mit Größenbegrenzung. Seit 0.4.3 besitzt das Dateijournal einzelne atomare Operationsdateien; die History-Ansicht liest weiterhin den Gesamtindex. Sehr große Langzeitarchive benötigen perspektivisch eine Datenbank. Das Journal schreibt pro erfolgreicher Datei einen dauerhaften Zustand und prüft Hashes; diese Integritätsarbeit kostet Zeit. Ähnliche Bilder werden als Prüfkandidaten gemeldet, mit maximal 10.000 Paaren pro Suche. Keine pauschale Zusage, dass jeder Bildtyp und jeder Ordner schneller wird.

## Gemessener Abschlusslauf

Millisekunden; Median aus fünf Läufen. Der jeweilige erste Lauf ist enthalten. Rohdaten aller Zwischenmessungen bleiben ebenfalls unter `tests/output/performance-after-*.json` erhalten.

| Messung | Vor Ausbau | Abschlusslauf |
| --- | ---: | ---: |
| JPEG 24 MP, vollständiges Lesen | 364.84 | 335.23 |
| PNG 24 MP, vollständiges Lesen | 684.34 | 339.27 |
| Fenster ohne Bild, Offscreen | 28.46 | 29.94 |

24-MP-JPEG im Dokumentcache: erster Zugriff **826.20 ms**, folgende vier Zugriffe **0.34–0.54 ms**. Das misst die Bereitstellung des Dokuments, nicht das anschließende Zeichnen im Fenster.

10.000-Datei-Ordner: einmaliger Indexaufbau **6570.28 ms** im Hintergrund. Anschließend 10.000 Positionszugriffe: **1.61 ms** über die Hashtabelle; derselbe Zugriffssatz mit linearer Suche **1689.27 ms**. Der Index wird beim normalen Weiterblättern wiederverwendet. Der erste Aufbau ist in dieser Umgebung weiterhin nicht sofort fertig.

Die unkorrigierten Dekodierzeiten schwanken stark: Der erste Nachherlauf lag etwa bei 760–1.099 ms für JPEG, während spätere vollständige JPEG-Läufe etwa 334–516 ms erreichten. Auch PNG und Indexaufbau schwanken. Daraus lässt sich keine verlässliche allgemeine Decoder-Beschleunigung ableiten. Klar nachgewiesen sind die sehr kurzen Cachetreffer, die Positionssuche sowie die funktionale Wiederverwendung der Ordnerliste. Die neue Oberfläche wird nicht als pauschal schneller ausgewiesen.

## Einstellungen seit 0.4.1

Unter Optionen → Einstellungen → Leistung sind beide Cachebudgets einstellbar (0 = aus), ebenso eingebettete RAW-Vorschauen und die anfängliche Batchparallelität. Die hier dokumentierten Messungen der Version 0.4 verwenden die unveränderten Standardbudgets 192/16 MiB. Die neue Icon-Engine rastert eigene SVG-Konturen bei Bedarf und verwendet den Qt-Pixmapcache; es werden keine externen Icondateien oder weiteren Laufzeitbibliotheken benötigt.


## Review 0.4.2: Größe und Katalog

Das bestehende vollständige Windows-Paket belegte vor den neuen Sprachdateien 189,29 MB (dezimal), als ZIP etwa 70,34 MB. uxzView.exe selbst war 1,88 MB groß. Qt Core/Gui/Widgets zusammen: 25,70 MB; ICU-Sprachdaten allein: 33,12 MB. Ein lokaler Abhängigkeitsvergleich ohne HEIF/AVIF/JPEG XL/EXR/JPEG 2000/RAW ergab rund 74 MB weniger PE-Dateien. Das ist eine Größenanalyse, kein bereits ausgeliefertes kleineres Paket: Diese Formate bleiben im vollständigen Paket enthalten. Ein schlankeres Standardpaket benötigt gezielt separat installierbare Codec-Pakete oder einen eigenen kleineren Qt-Build.

Die Bildverwaltung hält beim Öffnen einen Katalog-Snapshot. Die Suche liest/parst den JSON-Katalog nicht mehr bei jedem Tastendruck zweimal, sondern filtert den Snapshot mit 180 ms Eingabebündelung. Neue Bewertungen und der Metadatenindex aktualisieren den Snapshot. Bildcache-Schlüssel enthalten zusätzlich native Dateikennung und Änderungszeit in nativer Präzision; Batch-Vorschaupläne prüfen zusätzlich Inhalts-Hashes.


Erneuter Kontrolllauf mit 0.4.2 (Windows 11, Qt-Offscreen, warmer Dateisystemcache): Fenster-Median 18.41 ms; 24-MP-JPEG vollständig 416.96 ms, PNG 395.06 ms; 24-MP-JPEG-Cachetreffer 0.49–1.23 ms. Ordnerindex für 10.000 Dateien 3113.00 ms. Die neue Dateirevisionsprüfung bleibt damit bei Cachetreffern im Millisekundenbereich. Diese schwankenden Einzelmessungen belegen keine allgemeine Decoder-Beschleunigung. Rohdaten: `tests/output/performance-0.4.2.json`.

## Paket und Dateiabläufe 0.4.3

Der vor dem Umbau gemessene portable Ordner belegte 189.96 MB, das ZIP 73.03 MB (dezimal). Mit neu gebauten libheif/libavif und gezielter Plattformplugin-Auswahl benötigt die Laufzeit rund 154 MB, also etwa 19 % weniger. RAW, HEIC, AVIF, JPEG XL, EXR und JPEG 2000 bleiben im Paket. Kvazaar ersetzt x265 für HEVC; AOM übernimmt AV1 ohne die zusätzlichen SVT-/rav1e-Encoder. Das spart Bibliotheken, kann aber Geschwindigkeit und Kompression beim Speichern verändern. Der Paketierer begrenzt den Betrachter weiterhin auf 160 MB; die jetzt enthaltenen QR-/OCR-Werkzeuge und fünf Sprachen erhalten ein separates 25-MB-Budget. Sie belegen rund 24 MB und starten nur beim Aufruf einer Erkennung, ohne zusätzliche Hintergrundprozesse.

Die komplette portable Funktions- und Qualitätssuite läuft gegen diese tatsächlich ausgelieferten Bibliotheken. Vier Profilvergleiche, HDR und fünf Kamera-RAWs sowie ein HEIC-Farb-/Alpha-Test bestehen. Das ist kein allgemeiner Bildqualitäts- oder Encoder-Leistungsbenchmark.

ICU-Daten allein beanspruchen weiterhin 33.12 MB, Qt Core/Gui/Widgets zusammen etwa 25.70 MB. Ein wesentlich kleinerer Vollausbau braucht einen angepassten Qt-/ICU-Build oder separat installierbare Formatmodule. Die bereits bei Bedarf geladenen Module sparen Startarbeit, aber keinen Speicherplatz im Download.

Dateijournal-Schreibvorgänge ändern nur noch den betroffenen Eintrag und einen kleinen Sequenzzähler. Der Regressionstest übernimmt eine Legacy-Historie über 32 MiB und prüft, dass eine spätere Operation die erste Eintragsdatei weder neu schreibt noch verändert. Bewertungen für eine ganze Auswahl werden zusammen gespeichert. Bei Wasserzeichen prüft der Batchplan die Größenbegrenzung vor dem Hashing; die Bildressource wird einmal pro Lauf dekodiert und über alle Worker geteilt.

Kontrolllauf 0.4.3 nach dem Zustandsumbau (Windows 11, Release, Qt-Offscreen, warmer Dateisystemcache): Fenster-Median 9.26 ms, erster Fensterlauf 57.92 ms; 24-MP-JPEG vollständig 297.96 ms, PNG 304.44 ms. Cache: erster JPEG-Zugriff 293.80 ms, danach 0.237–0.336 ms. Ordnerindex für 10.000 Dateien 2210.14 ms; 10.000 Positionszugriffe 0.61 ms mit Hash beziehungsweise 578.56 ms linear. Diese Momentaufnahme zeigt keine erkennbare Verlangsamung der gemessenen Abläufe, ersetzt aber keinen kontrollierten plattformübergreifenden Leistungsvergleich. Rohdaten: `tests/output/performance-0.4.3.json`. Der Benchmark misst keine RAW-Entwicklung; die Qualitätslaufzeiten der fünf RAWs sind separat im Testprotokoll enthalten.

## Ordner und Nachbarbilder – 20.10.2026

Windows 11, Qt 6.11.2, Release und Offscreen; derselbe deterministische Ordner mit 10.000 Dateien, warmer Dateisystemcache. Die drei Messungen je Indexvariante enthalten das Einlesen, die Sortierung und den Aufbau der Positions-Hashtabelle. Die Rohdaten liegen in `tests/output/profile-2026-10-20-before-extended.json` und `profile-2026-10-20-after.json`; diese lokalen Messdateien werden nicht mit dem Source-ZIP ausgeliefert.

| Ordnerindex, Median in ms | Vorher | Nachher |
| --- | ---: | ---: |
| Eine Dateiendung | 6316.42 | 30.02 |
| Alle unterstützten Dateiendungen | 4632.48 | 49.69 |

Hauptursache war die vorgeschaltete Leseberechtigungsprüfung je Datei. Der Nachherlauf misst im selben Prozess für die bisherige Enumeration einschließlich dieser Prüfung noch rund 2660 ms. Der Index verwendet nun [QDirListing](https://doc.qt.io/qt-6.8/qdirlisting.html) ohne diese Vorabprüfung. Der Decoder prüft den tatsächlichen Zugriff beim Öffnen; eine aufgelistete Datei kann deshalb weiterhin nicht lesbar sein und eine normale Lesefehlermeldung auslösen. Natürliche/alphabetische Sortierung, Datum, Dateigröße, Rückwärtsreihenfolge, Unicode, Datei-/Verzeichnisfilter, verlinkte Dateien und der neueste Hotfolder-Eintrag bleiben berücksichtigt. Abbruch wird schon vor und während der Enumeration geprüft.

Lesende Zugriffe auf die gemeinsam genutzte Ordnerliste verwenden konstante Qt-Zugriffe. Dadurch löst Navigation keine unbeabsichtigte Copy-on-write-Kopie der gesamten Liste aus. Die Nachbarleiste verwendet überlappende Schaltflächen und unveränderte Icons wieder; bei einem Schritt mit fünf Nachbarn je Seite bleiben acht von zehn Schaltflächen erhalten. Dateirevisionen werden weiterhin in Vorschau-Workern geprüft. Geänderte Dateien erhalten eine neue Vorschau, gelöschte/fehlerhafte Dateien einen Platzhalter; ein frischer Verzeichnis-Snapshot setzt die Widgets zurück.

Die Messung ist kein allgemeiner Beschleunigungsfaktor für das gesamte Programm. Decoder, RAW-Entwicklung und Farbmanagement wurden in diesem Schritt nicht geändert. Getrennte Decoder-/Metadaten- und 8-/16-Bit-Zeichenmessungen im selben Profiler schwanken deutlich mit der Rechnerlast. Der große Unterschied beim Index und die Wiederverwendung der Listendaten/Widgets sind die hier gezielt geprüften Änderungen.

Reproduzieren (erst den bestehenden Benchmark einmal für die Fixtures ausführen):

```powershell
$env:PATH="$PWD/.deps/sdk/ucrt64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
./build/ixview_benchmarks.exe tests/output/performance-local.json
./build/ixview_benchmarks.exe tests/output/profile-local.json --profile
./build/ixview_benchmarks.exe tests/output/preview-profile-local.json --profile-previews
```

`--profile` zerlegt Enumeration, Sortierschlüssel, Index mit einer/allen Endungen, Decoder, Metadaten und wiederholtes Zeichnen. `--profile-previews` vergleicht im selben Prozess eine überlappende Nachbargruppe mit einem erzwungenen Neuaufbau; die Reihenfolge wechselt. Beide verwenden dieselben Miniaturen im warmen Cache. Die UI-Zeit enthält den Listenwechsel und die aktuellen GUI-Ereignisse, nicht die vollständige Decoderlaufzeit.

Kontrollierter Miniaturvergleich im selben Prozess, Median aus je 30 wechselnd angeordneten Messungen: erzwungener Neuaufbau **4.10 ms**, wiederverwendete Gruppe **3.86 ms**. Der Unterschied ist klein und wird nicht als belastbare allgemeine Beschleunigungsquote ausgewiesen. Gesichert sind die acht erhaltenen Schaltflächen und unveränderten Icons sowie das Ausbleiben unnötiger Kopien der gesamten Ordnerliste. Rohdaten: `tests/output/preview-profile-2026-10-20-controlled.json`.


## Auswahlrahmen und Metadaten – 20.10.2026

Auswahländerungen zeichnen nur die alten und neuen Rahmenkanten neu. Unveränderte Auswahlen senden kein weiteres Änderungssignal; Bildinhalt, Gitter und Hintergrund bleiben in den übrigen Bereichen erhalten. Dafür wird kein zusätzlicher Bildcache angelegt. Ein Pixelvergleich prüft, dass bei mehreren gebrochenen Zoomstufen und 8-/16-Bit-Quellen jeder tatsächlich veränderte Pixel innerhalb der angeforderten Zeichenregion liegt; Verschieben, Vergrößern, winzige/geclippte Rahmen und Entfernen sind enthalten.

Kontrollierter Vergleich im selben Prozess, 24-MP-Bild, 1200×800-Widget, Windows 11/Qt 6.11.2/Release/Offscreen. Je 40 Messungen mit wechselnder Reihenfolge; Median in Millisekunden. Beide Varianten verwenden die identischen Originalpixel und dieselbe geglättete Anzeige. Der Kontrollpfad fordert nach jeder Auswahländerung zusätzlich die ganze Bildfläche zum Neuzeichnen an.

| Auswahl verschieben | Ganze Fläche (Kontrolle) | Nur betroffene Kanten |
| --- | ---: | ---: |
| 8 Bit | 6.21 | 1.13 |
| 16 Bit | 25.10 | 1.70 |

Mit Exiv2 ab 0.28 liest ein schreibgeschützter Qt-I/O-Adapter die benötigten Dateibereiche über einen 64-KiB-Puffer. Damit entfällt die zuvor bedingungslose vollständige Dateikopie vor dem Metadatenlesen. Die Position wird unabhängig vom Dateihandle gehalten, sodass nahe Sprünge zwischen PNG-Chunk-Headern im Puffer bleiben. Qt übernimmt die Unicode-Dateinamen. Parser, die eine zusammenhängende Speicheransicht benötigen, erhalten weiterhin einen eigenen, auf die bisherige 128-MiB-Quelldateigrenze begrenzten Snapshot. Es gibt keine ungeschützte OS-Speicherabbildung einer extern veränderbaren Datei. Für Exiv2 0.27 bleibt der bisherige Speicherpfad erhalten.

Separat gemessenes Metadatenlesen derselben 24-MP-Fixtures, warmer Dateisystemcache, Median aus sieben Aufrufen: JPEG **19.33 → 0.54 ms**, PNG **64.49 → 61.32 ms**. PNG-Nachhermediane schwankten in zwei gepufferten Läufen zwischen 29.38 und 61.32 ms; daraus wird keine belastbare allgemeine PNG-Beschleunigung abgeleitet. Die JPEG-Messung betrifft nur Metadaten, nicht das vollständige Öffnen oder Dekodieren. EXIF/IPTC/XMP werden aus einer schreibgeschützten Unicode-JPEG-Datei mit 16 MiB angehängten Daten und aus einem PNG über mehrere Puffergrenzen geprüft; die übrigen Codec-/RAW-/Farbtests bleiben enthalten.

Rohdaten: `tests/output/interaction-before.json`, `interaction-buffered.json` und `interaction-final.json`. Zusammenfassung lokal unter `build/performance-2026-10-20-interaction-validation.json`. Messungen mit warmem Cache und begrenzter Dateiauswahl ersetzen keine Desktop-, Netzlaufwerk- oder RAW-Leistungsprüfung.

Reproduzieren nach dem normalen Benchmarklauf zur Fixture-Erstellung:

```powershell
$env:PATH="$PWD/.deps/sdk/ucrt64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
./build/ixview_benchmarks.exe tests/output/interaction-local.json --profile-interaction
```


## APNG und Undo/Redo – 20.10.2026

Der APNG-Decoder hält für eine Dateirevision einen Kompositionszustand. Normale Vorwärtswiedergabe dekodiert und komponiert nur neue Frames; bereits ausgegebene QImages bleiben durch Copy-on-write unverändert. Rückwärtssprünge beginnen wieder mit einer leeren Leinwand. F5/Cache-Leeren und geänderte native Dateirevisionen verwerfen den behaltenen Zustand. Unterschiedliche aktive Sitzungen können unabhängig arbeiten; der globale Cache-Lock umfasst keine Datei-I/O oder Komposition.

Das Budget von 64 MiB gilt für den verrechneten behaltenen Sitzungszustand aus komprimierten Frames, Leinwand, Disposal-Snapshot und Metadatenreserve. Überschreitet eine Sitzung dieses Budget, wird sie nach dem Lesen nicht behalten; spätere Frames müssen dann erneut abgespielt werden. Aktive Decoder, Dokumentcache, ausgegebene Bilder und temporäre Puffer kommen separat hinzu. Dies ist keine globale RAM-Grenze und keine Zusage linearer Laufzeit für übergroße Animationen.

Kontrollierter Vergleich auf Windows 11, Qt 6.11.2, Release/Offscreen: deterministische 128×128-APNGs mit vollflächigen deckenden Frames, warmer Dateisystemcache, drei wechselnd angeordnete Durchläufe je Variante. Der Kontrollpfad leert vor jedem Frame die neue Decodersitzung und reproduziert damit die bisherige Wiederholungsarbeit. Jeder Durchlauf beginnt mit leerem Dokumentcache. Median in ms bei ausgeschaltetem Dokumentcache:

| Frames | Zustandslose Kontrolle | Behaltene Sitzung | Kompositionen vorher → nachher |
| --- | ---: | ---: | ---: |
| 20 | 79.14 | 22.78 | 210 → 20 |
| 40 | 182.24 | 45.83 | 820 → 40 |
| 80 | 535.45 | 84.40 | 3240 → 80 |

Bei 16/192 MiB Dokumentcache bleiben die Kompositionszahlen identisch; die Nachhermediane liegen je nach Fall zwischen 19 und 104 ms. Die kleine synthetische Datei belegt den vermiedenen quadratischen Wiederaufbau, keinen allgemeinen Geschwindigkeitsfaktor für GIF, RAW oder große Animationen. Rohdaten lokal unter `tests/output/animation-profile-2026-10-20.json`.

Undo und Redo berechnen Farbmanagement und optionale Anzeigeschärfung im bestehenden Worker. Erst nach erfolgreicher Anzeigevorbereitung werden die Historienstapel und das Dokument gemeinsam übernommen. Die Regression prüft die noch unveränderten Pixel direkt nach dem Auslösen sowie weiterlaufende GUI-Ereignisse und das fertige Ergebnis; sie ist kein Desktop-Framerate-Benchmark.

```powershell
$env:PATH="$PWD/.deps/sdk/ucrt64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
./build/ixview_benchmarks.exe tests/output/animation-profile-local.json --profile-animations
```

## Anzeige-Cache und Dateirevisionen – 23.10.2026

Ein verzögert angelegter Rastercache hält ausschließlich die eingepasste Bildanzeige samt Hintergrund, höchstens 32 MiB pro Ansicht. Auswahl, Gitter und Informationsanzeige bleiben getrennt. Die erste Darstellung zeichnet direkt; erst ein weiteres großes Neuzeichnen legt den Cache an. Kleine Auswahländerungen erzwingen keinen vollständigen Aufbau. Quelle, Zoom, Position, Viewport, DPI, Hintergrund und Interpolation gehören zum Schlüssel. Fließkomma-/HDR-Quellen, nicht deckende Hintergründe, ungeeignete Transformationen und übergroße Flächen verwenden den direkten Zeichenpfad. Bildcache-Budget null deaktiviert auch diesen Cache. Originalpixel und Exportpräzision ändern sich nicht.

Kontrollmessung im selben Prozess, Windows 11, Qt 6.11.2 Release, 24 MP, 1200×800-Ansicht, Median in ms:

| Wiederholte Anzeige | Cache aus | Cache an |
| --- | ---: | ---: |
| 8 Bit, volle Zeichenfläche mit Auswahl und GUI-Ereignissen | 3.77 | 1.15 |
| 16 Bit, volle Zeichenfläche mit Auswahl und GUI-Ereignissen | 9.74 | 1.13 |
| 8 Bit, nur geänderte Auswahlkanten | 0.60 | 0.40 |
| 16 Bit, nur geänderte Auswahlkanten | 0.84 | 0.36 |

Die Windows-Dateirevisionsabfrage nutzt Größe, Identität und hochauflösende Zeitstempel des bereits geöffneten Handles. POSIX verwendet seinen bestehenden stat-Snapshot; bei fehlenden nativen Angaben bleibt der QFileInfo-Fallback. 1.000 Abfragen derselben Fixture benötigen lokal **376.72 → 126.97 ms**, in getrennten Builds bei warmem Dateisystemcache. Das ist kein Faktor für die gesamte Anwendung oder das Dekodieren. Der vollständige 24-MP-JPEG-/PNG-Decode bleibt in diesem Lauf bei rund 350/343 ms; der Index mit 10.000 Dateien bei 53 ms.

Pixelvergleiche prüfen Cache und direkten Pfad bei 100/125/150/200 Prozent Skalierung, 8-/16-Bit- und Fließkommaquellen, Alpha, gebrochenem Zoom, Größenwechseln und Overlays. Der [kritische Prüfbericht](REVIEW_2026-10-23.md) enthält weitere Messungen und die offenen Prioritäten. Globale RAM-Budgetierung und ein koordiniertes Vorladen bleiben nächste größere Aufgaben; mehr gleichzeitige Decoder allein sind keine sichere Beschleunigung.

## Native fullscreen-exit latency (2026-10-23)

Windows 11, Release, Qt 6.11.2, native Windows platform plugin. The same three test cases cover normal/maximized windows, previews on/off, zoom restoration and a 20,003-pixel-wide image. Nine completed exits per run exclude cancelled covers. Timing starts before capture and ends when the cover is destroyed; it does not measure screen photons.

An earlier optimization reduced a local median from 484 to 111 ms, but removed the initial compositor fence. The subsequent user report showed why that fast path was insufficient: [GdiFlush](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gdiflush) only submits the thread’s drawing batch, and cannot ensure the new cover is displayed before the owner shrinks. The current path restores one initial [DwmFlush](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush), restores the owner’s DWM setting while still covered, and retains the final presentation fence and queued geometry verification. Repeated interval delays and redundant redraws remain removed.

Earlier paired local samples (Qt cover, before the native snapshot replacement), with considerably higher desktop load than the earlier run:

| Measurement | GDI-only cover | Cover presented first |
| --- | ---: | ---: |
| Median complete exit | 203.43 ms | 294.19 ms |
| Slowest complete exit | 1236.48 ms | 1227.16 ms |
| Median cover ready | 20.49 ms | 96.47 ms |

This is a correctness/latency tradeoff, not a new speedup claim. Synchronizing presentation adds actual waiting time; there is no fixed sleep and no guarantee against all GPU/driver-specific flicker. The wide variation means these samples establish neither a latency bound nor independent compositor timings. Native tests require successful cover presentation before native exit resizes, preserve the cover through pending Qt work, and cover four DPI scales. Further latency work must keep that ordering intact. Raw local reports: `build/fullscreen-latency-gdi.json` and `build/fullscreen-latency-after.json`.

To collect phase timings without adding UI controls or background work:

```powershell
$env:PATH="$PWD/.deps/sdk/ucrt64/bin;$env:PATH"
$env:IX_PROFILE_TRANSITIONS="1"
./build/ixview_tests.exe fullscreenTransitionsRetainVisibleWindow fittedImageHasNoPhantomScrollbarsAndWheelNavigates fullscreenFitsWholeImageWithoutFrame -platform windows -o build/fullscreen-latency.txt,txt
Remove-Item Env:IX_PROFILE_TRANSITIONS
```

JSON timing records appear in the Qt test output. `presentationPasses == 0` identifies cancellation/cleanup before the queued presentation path; do not mix those records with completed exits. Profiling is disabled by default.


## RAW-Vorschauen und Abbruchwarteschlange – weitere Prüfung am 23.10.2026

Eingebettete RAW-JPEGs werden mit angeforderter Zielgröße dekodiert. Zuvor wurden auch große eingebettete Vorschauen erst vollständig dekodiert und dann verkleinert. Bitmap-Vorschauen werden vor einer nötigen Orientierungskorrektur reduziert. Die gemeinsame RAW-Endungsliste verhindert, dass seltenere Endungen für Miniaturen versehentlich zum vollständigen Decoder gelangen. Fehlt eine eingebettete Vorschau, bleibt der Platzhalter erhalten.

Kontrollvergleich im selben Prozess: Windows 11, Qt 6.11.2 Release/Offscreen, sieben abwechselnd angeordnete Aufrufe je Variante, warmer Dateisystemcache und jeweils geleerter Miniaturcache. Die Kontrolle lädt das vor der Änderung gebaute RAW-Modul; das neue Modul läuft über den normalen Vorschaupfad einschließlich Dateirevisionsprüfung. Beide liefern dieselben Abmessungen. Median in Millisekunden:

| RAW-Datei | Zielgrenze | Vorher | Nachher |
| --- | --- | ---: | ---: |
| canon-eos7d.cr2 | 68x50 | 107.34 | 37.08 |
| canon-eos7d.cr2 | 512x512 | 102.96 | 37.13 |
| nikon-d7000.nef | 68x50 | 79.05 | 22.35 |
| nikon-d7000.nef | 512x512 | 159.23 | 52.67 |
| pentax-k10d.dng | 68x50 | 124.82 | 50.51 |
| pentax-k10d.dng | 512x512 | 124.54 | 66.02 |
| pentax-k10d.pef | 68x50 | 123.88 | 51.37 |
| pentax-k10d.pef | 512x512 | 126.38 | 68.39 |
| sony-a6000.arw | 68x50 | 41.52 | 24.62 |
| sony-a6000.arw | 512x512 | 43.04 | 34.47 |

Das entspricht bei diesen Dateien etwa 41–72 % weniger Zeit für kleine Nachbarvorschauen und 20–67 % für die größere Vorschau. Diese Zahlen betreffen eingebettete Vorschauen einschließlich RAW-Dateiparsing, nicht die vollständige RAW-Entwicklung, kalte Datenträger oder alle Kameramodelle. Die Qualitätsprüfung vergleicht zusätzlich kleine/große Vorschauinhalte und Orientierung; volle 16-Bit-RAWs und ihre Exportpixel bleiben im bisherigen Prüfpfad. Quelldaten: `tests/output/raw-previews-speed-review.json` (lokales Messartefakt).

Die Vorschauwarteschlange erzeugt nur für höchstens zwei aktive Decoder Futures/Watcher. Bei einem Bildwechsel werden veraltete wartende Anfragen sofort verworfen. Der Belastungstest reicht 200 Gruppen mit jeweils zehn Anfragen ohne GUI-Pause ein: höchstens zwei aktive Watcher, danach ausschließlich das Ergebnis der neuesten Generation. Das reduziert Ressourcen und ausstehende GUI-Abschlussereignisse; es ist keine gemessene allgemeine Beschleunigungsquote für Ordnernavigation. Ein bereits laufender Fremddecoder ist weiterhin erst nach seiner Rückkehr abbrechbar.

Die anfängliche und abschließende Vollbild-Präsentationssynchronisation bleiben erhalten. Diese Prüfrunde beansprucht keine zusätzliche Beschleunigung des Vollbildwechsels.

Reproduzieren nach dem normalen Benchmarklauf und dem Herunterladen der Qualitäts-Fixtures:

```powershell
$env:PATH="$PWD/.deps/sdk/ucrt64/bin;$env:PATH"
$env:QT_QPA_PLATFORM='offscreen'
./build/ixview_benchmarks.exe tests/output/raw-previews-local.json --profile-raw-previews
```

Ohne `IX_RAW_BASELINE_MODULE` enthält der Bericht nur den aktuellen Pfad. Für den Vergleich zeigt diese optionale Variable auf eine vor der Änderung aufbewahrte Moduldatei mit `ixReadRawPreview`; solche lokalen Binärdateien gehören nicht in Releases oder das Repository.


Erneuter nativer Vollbild-Kontrolllauf nach dieser Änderung: neun abgeschlossene Rückwechsel, Median 295.00 ms, Maximum 730.48 ms, jeweils drei Präsentationsprüfungen. Der Median liegt praktisch beim vorherigen Lauf (294.19 ms); daraus wird kein Geschwindigkeitsgewinn abgeleitet. `build/fullscreen-latency-after.json` enthält jetzt diesen letzten Kontrolllauf; der obige frühere Vergleich dokumentiert weiterhin die Entscheidung zur zusätzlichen ersten Präsentationssynchronisation.


## Fullscreen cover content readiness (2026-10-23)

A subsequent first-paint probe reproduced two owner resizes while the Qt cover was still unpainted, despite its successful DwmFlush flag. A compositor fence cannot submit a pending Qt expose/paint. The Windows cover now receives a complete native bitmap via [UpdateLayeredWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-updatelayeredwindow) before it is shown, with opaque content and native physical-pixel bounds. Temporary bitmap/DC resources are released immediately after upload. Initial and final presentation fences remain unchanged; no fixed waiting interval has been added. This is a correctness fix, not a general speedup claim.

The native regression attempts to read a known screen pixel after the owner changes colour and leaves fullscreen, without processing intervening Qt events. If the desktop denies all pixel readback, it records a warning instead of claiming visual verification; the remaining native assertions still run. The current local test session denies readback, so the pixel-colour assertion remains unverified here. It also checks physical coverage, no-focus/input-transparent styles, normal/maximized restoration, previews, rapid replacement and owner destruction. The public native runner now repeats the transition regression at 100/125/150/200 percent scaling. These local checks cannot establish flicker-free output on every driver, HDR display or multi-monitor configuration.

A local control run of the three targeted regressions (including the new solid-colour probe) recorded 11 completed exits: median 267.61 ms, maximum 683.55 ms. This is not a paired comparison with the earlier nine-exit runs and does not establish a speedup or worst-case bound. Local phase records: `build/fullscreen-native-cover-timings.json`.


## Random slideshow and export buffers (2026-10-24)

Random selection now shuffles once per round and removes the last entry, rather than rebuilding folder membership and removing a random middle entry for every image. A shared directory snapshot detects when removals actually need filtering; new files join the next round.

Five alternating in-process release-build samples on Windows 11 measured 1,000 selections from 10,000 unchanged paths: median **1,660.95 ms before, 1.01 ms after**. Ranges: 1,634.56–2,009.65 ms before, 0.82–1.08 ms after. This measures selection bookkeeping only, including new-round setup for the controller; reading, decoding and painting are excluded. The reference algorithm's initial list preparation is outside its timer. It does not establish a general viewer speedup.

```powershell
./build/ixview_benchmarks.exe tests/output/slideshow-local.json --profile-slideshow -platform offscreen
```

Local results: `tests/output/slideshow-review-2026-10-24.json`. The benchmark also verifies unique choices and exclusion of the currently displayed file.

Exports without metadata rewriting write directly into the atomic staging file. This removes an additional byte array as large as the encoded output; it does not remove codec-internal buffers or bound total process memory. Metadata-bearing exports keep their rewrite buffer. Exact pixels, ICC, DPI and failure preservation are covered by the workflow suite; no measured export speedup or peak-memory reduction is claimed.

## Cached RAW loading in 0.4.4 (2026-10-24)

The full-image cache is now checked before requesting an embedded RAW preview. A cached original no longer triggers a smaller preview or its delivery callback. On a miss, the preview still runs before full decoding, outside the cache mutex; source revision validation covers both operations.

Windows 11, Qt 6.11.2 Release/Offscreen, one Pentax K10D DNG, five alternating samples per setting, warm filesystem and cached original. Median milliseconds:

| Preview cache | Previous preview-first path | Current cache-first path |
| --- | ---: | ---: |
| Default, 16 MiB | 0.415 | 0.202 |
| Disabled, 0 MiB | 64.867 | 0.216 |

The absolute saving with the default preview cache is small. Disabling that cache previously repeated embedded-preview extraction even when the original was cached. Preview deliveries fell from five to zero in both cases. Every sample retains the cached original and identical display pixels. Timings include display conversion but exclude GUI painting; they do not establish gains for first RAW development, uncached originals, all camera models or general folder navigation.

```powershell
./build/ixview_benchmarks.exe tests/output/raw-cache-044.json --profile-raw-cache -platform offscreen
```

The fixture comes from `scripts/fetch_quality_fixtures.py`. Local raw samples: `tests/output/raw-cache-044.json`. See [the 0.4.4 review](REVIEW_0.4.4.md) for the associated regression checks and remaining work.

## Resize image-buffer accounting in 0.4.6 (2026-10-25)

Nearest-neighbor resizing is no longer charged for an unused 16-bit separable-filter intermediate. Smooth resizing now includes simultaneously live input/output conversions and sharpening peaks when enforcing its image-buffer budget. The [data-path review](REVIEW_0.4.6.md#allocation-accounting) records exact buffer calculations and allocation regressions. Some formerly accepted smooth jobs now fail early because their actual image buffers exceed the stated 512 MiB operation budget; these cases previously passed an incomplete estimate. This is not a measured speedup or total process-memory cap. Shared RAM accounting, filter-weight/allocator overhead and streaming/tiled processing remain separate work.

## Superseded directory scans in 0.4.7 (2026-10-25)

The main window previously passed no cancellation token to the directory scanner. A changed folder or sort order waited for the complete old enumeration/sort before starting the replacement. DirectoryIndexService now supplies the token, cancels active work and retains only the latest pending request on a single low-priority worker. Ordinary navigation in an unchanged folder keeps its installed index.

Semaphore-controlled regressions confirm that three requests run only the active and final scanner, that the active scanner receives cancellation, and that only the latest result is delivered. This establishes avoided obsolete work and bounded queuing, not a measured folder-opening speedup. Cancellation is cooperative between filesystem entries; an already-blocked filesystem call or a running standard-library sort can still finish before cancellation is observed. Sorting, network drives and huge folders need separate timing coverage. See [the review](REVIEW_0.4.7.md).

## Command search and gallery presentation in 0.4.8 (2026-10-25)

The optional command-search model now retains its items while typing. A persistent-index regression and model reset/insertion observers verify that further query characters reuse the existing source rows. Action text, availability and destruction update those rows directly, so avoiding reconstruction does not leave stale states. The earlier code invalidated those rows on every keystroke; the initial regression also reproduced empty-Enter replay and stale/deleted actions. The alias table is shared and immutable. The model remains loaded while the optional search widget exists; this removes allocation churn, not all steady-state memory.

Neighbor previews also retain the current-image label, and gallery filtering stops after the first unmatched term. These changes avoid unnecessary work without a timing claim for whole-image decoding or large folders. Whole-JSON catalog snapshots, rebuilding all filtered list items and filesystem-stat calls in GUI status refreshes remain significant candidates. See [the efficiency/style review](REVIEW_0.4.8.md).

## First image in an already-running window (2026-10-25)

The subsequent [26 October review](REVIEW_2026-10-26.md) removes repeated status
timestamp queries from the GUI thread and adds bounded per-process diagnostics.
Its full Windows/DPI validation supersedes the narrower validation below.
The original timeout remains unexplained; the new tooling retains checkpoints
for any recurrence. The historical before/after timings below remain tied to
their original profiler/window conditions.

The first installation of a document synchronously resolved all folder image
extensions on the GUI thread. That reads every image-format plugin's metadata,
even though only the opened image's decoder is needed before painting. A traced
4x3 PNG run spent 125.87 ms in folder setup after the image was decoded. Two
separate settings scopes also wrote recent files and the last folder; the
isolated INI profile spent 47.09 ms there. Command/status refreshes were below
one millisecond each in that sample.

MainWindow now requests format discovery on the existing low-priority directory
worker. Explicit scanner filters keep their previous behavior. The first image
can paint while the folder is indexed; queued navigation still waits for its
valid snapshot. Recent-file/directory updates share one QSettings scope and
unchanged values are not written. The decode, metadata, precision, color
conversion and original-image ownership paths are unchanged.

`ixview_benchmarks` has an early `--profile-first-open` mode that avoids preparing
the unrelated large-image/10,000-file fixtures. It creates a temporary INI
profile, shows an empty window for 200 ms, opens one selected file three times
and clears document caches before each open. Only the first sample in each
fresh process is compared below. A viewport paint event schedules an observation
after drawing; this is not proof of compositor/display completion. A GUI timer
records the largest heartbeat gap during the open. OS caches are not cleared.

Windows 11, Qt 6.11.2 Release, same retained baseline and corrected benchmark,
alternating before/after order. PNG is the 4x3 smoke fixture; JPEG is the
1920x1041 README example. Medians in milliseconds:

| Mode | Image | First paint before | First paint after | GUI gap before | GUI gap after |
| --- | --- | ---: | ---: | ---: | ---: |
| Offscreen, five samples | PNG | 232.89 | 67.35 | 204.06 | 51.61 |
| Offscreen, five samples | JPEG | 337.34 | 113.22 | 291.05 | 68.39 |
| Windows windows, three samples | PNG | 141.02 | 68.57 | 123.76 | 60.64 |
| Windows windows, three samples | JPEG | 152.47 | 84.30 | 115.04 | 32.04 |

These measurements establish reduced first-image presentation delay for these
fixtures and this profile, not faster decoding or a guarantee for all images.
Native JPEG document-install observation varied from 25.78 ms before to 38.41 ms
after despite the shorter total presentation time; no decoder speedup is
claimed. Native/offscreen environments and background load differ, so compare
each row internally. Larger files, cold disks, network paths and antivirus
effects remain outside this measurement.

An initial sandboxed native baseline took 1671.10 ms, and a corrected sandboxed
benchmark process subsequently timed out after 40 seconds. The precise cause
of that timeout is unknown. A traced corrected native run outside the sandbox
and all 12 subsequent alternating native benchmark processes completed. The
failed attempt is not folded into the successful native medians and is not
treated as a proven sandbox-only issue. Its preceding sample is preserved as
`build/first-open-native-sandbox-before.json` and its temporary profile as
`build/first-open-RxgzXL`.

Raw comparisons: `build/first-open-comparison.json`,
`build/first-open-native-comparison.json` and the individual JSON samples.
Trace: `build/first-open-traced-detail.log`. The baseline executable is
`build/first-open-before.exe`, retained solely for reproducing the comparison.

```powershell
./build/ixview_benchmarks.exe build/first-open.json --profile-first-open example.jpg -platform windows
# Optional stage diagnostics, written without file paths:
$env:IX_PROFILE_IMAGE_LOAD='1'
$env:QT_FORCE_STDERR_LOGGING='1'
# Run the benchmark or app, then remove these variables after profiling.
```

The new workflow regression checks discovery, non-image exclusion, explicit PNG
filtering and empty-filter compatibility. The full local core run passes 30
Python tests and all five Qt suites: 155 main, 49 workflow, 13 quality, 12 storage
and 17 extension checks (246 total), with required RAW/tools and no skips.
Evidence: `build/first-open-core-validation.log`, `build/validation-summary.json`
and the retained suite reports. Four native navigation/fit/fullscreen command
cases plus setup/cleanup pass (six checks), as do all 49 native workflow checks.
The fullscreen test reports unavailable desktop-pixel readback, so this session
does not verify its cover color or establish flicker-free presentation.

The deployed SDK-free runtime also passes all 246 Qt checks without failures
or skips, actual QR/OCR processes with all five models, and relocated
profile/tool startup. Evidence: `build/first-open-native-test-results.txt`,
`build/first-open-native-workflow-test-results.txt` and
`build/first-open-portable.log`. Linux, macOS, Windows 10 and the unresolved
macOS workflow failure still require their separate validation.

The README images are also explicitly selected for public sources and installed
documentation. A regression checks local HTML image references against both
selections. This does not change image content or viewer decoding; it preserves
the README's existing assets when preparing the updated package. Release-note
generation refers to maintained evidence rather than repeating stale fixed
test counts.

## Avoid repeat decoding for cached PNG/BMP previews — 2026-10-26

On a thumbnail-cache miss, `loadPreview` previously reopened and decoded a file
even when its unchanged full-resolution original was already in the document
cache. It now borrows eligible original pixels by QImage implicit sharing and
scales them outside the cache lock. This adds no runtime dependency or persistent
cache budget. Source revision, page/animation state, modified state and format
policy still apply. Cancellation, allocation failure and revision changes are
checked after scaling, before a thumbnail is cached or returned.

Only single-page, nonanimated PNG/BMP originals qualify. JPEG retains its
decoder-scaled path; reduced JPEG decoding can avoid reading full-resolution
pixels and need not match full-resolution resampling exactly. RAW and other
formats also retain their established paths. See
[Qt QImageReader](https://doc.qt.io/qt-6.8/qimagereader.html) for decoder-dependent
scaling behavior and [QImage](https://doc.qt.io/qt-6.8/qimage.html) for sharing.

The new `--profile-preview-reuse` mode creates temporary deterministic 4000x3000
PNG/BMP fixtures, uses five alternating samples per arm and disables the exact
thumbnail cache in both arms. Both decode and retain an original before timing
the preview; one disables the document cache, the other enables it. Priming
costs are reported separately. Each preview is compared exactly with a smooth
resize of its original and the original's cache key must remain unchanged.

Repeated Windows 11 / Qt 6.11.2 Release run, warm OS filesystem, median ms:

| Format | Thumbnail bounds | Decode again | Borrow cached original |
| --- | --- | ---: | ---: |
| PNG | 68x50 | 155.42 | 4.70 |
| PNG | 512x512 | 322.09 | 9.87 |
| BMP | 68x50 | 156.85 | 6.59 |
| BMP | 512x512 | 154.23 | 7.10 |

These are thumbnail-operation timings with an already-decoded original, not
first-open or end-to-end browsing speedups. Original decode cost remains and
cache misses/oversized originals do not gain this shortcut. CPU/OS load varied
between runs; compare the alternating arms within each case. Baseline with the
same new profiler shows no fast path before the change. Evidence:
`build/preview-reuse-before.json`, `preview-reuse-after.json` and
`preview-reuse-after-repeat.json`. Fixtures are deleted automatically.

```powershell
./build/ixview_benchmarks.exe build/preview-reuse.json --profile-preview-reuse -platform offscreen
```

The regression fails on the old implementation for the eligible cached PNG.
It verifies PNG/BMP reuse with preserved alpha, precision and color profile;
unchanged originals; decoder fallback for animation, multiple pages, modified
documents, RAW and JPEG; and invalidation after a source rewrite.

Local verification: all 20 steps of
`python -B scripts/validate.py --suite all --require-assets` pass, including
35 Python tests and 256 Qt checks with no failures/skips (155 main, 59 workflow,
13 quality, 12 storage, 17 extension), native Windows suites and
100/125/150/200 percent scales. Evidence: `build/current-ci-validation.log`,
`build/validation-summary.json` and the retained suite reports. The first
sandboxed Python diagnostic attempt was denied access to the sandbox's temporary
directory; the complete run used the normal platform temporary directory and
passed without weakening tests. This remains Windows evidence, not confirmation
of the corrected Linux/macOS workflow or every graphics driver.

The deployed portable runtime also passes all 256 Qt checks without SDK paths,
real QR/OCR engine execution with all five models, and relocated profile/tools
startup (`build/current-ci-portable.log`). Dependency verification rechecks all
65 corresponding-source URLs without changing SDK/runtime library hashes.

### Further architectural candidates, not implemented

- Decode a viewport-sized JPEG first, and request original pixels only for
  zoom/edit/export. This requires an explicit proxy/original document state,
  exact selection coordinates and tests that proxy pixels never reach exports,
  Undo or recognition inputs. Existing RAW-preview separation is a useful base.
- Share identical in-flight reads between foreground loading and prefetch.
  Cancellation must detach individual subscribers rather than abort a decoder
  still needed by another consumer; benchmark latency and peak memory first.
- For huge JPEG zoom regions, evaluate decoder clipping/partial decoding before
  adding GPU textures. Tile uploads, color management and driver behavior can
  offset GPU gains. Existing libjpeg-turbo already uses CPU SIMD where supported.

These candidates avoid assuming that more threads, GPU work or another cache
automatically improves interactive latency. They require separate measurements
and ownership changes, not hidden quality reductions.

## Remove the redundant metadata-header probe — 2026-10-26

`QtCodec::read` opened a file and read up to 256 KiB to look for metadata
signatures before calling Exiv2. A successful Exiv2 result then overwrote that
coarse presence flag, including when the image has no metadata tags. The codec
now asks the existing metadata reader first and performs the signature probe
only when no metadata result is available. This removes one file open and its
read work on the successful path without changing pixel decoding or metadata
preservation. Unsupported formats and metadata-disabled builds keep the
conservative fallback and export-loss policy. No dependency/cache is added.

The new `--profile-metadata-open` mode performs 15 complete `CodecRegistry`
reads after one priming read, without the document cache. It checks repeated
pixels, profiles, tags, metadata flags and parser diagnostics for consistency.
On Windows it also records process I/O counters. The same profiler was linked
against the old/new core and run in alternating fresh processes: five per arm
per fixture, repeated with reversed case/arm order (60 processes total).
Fixture dimensions, file lengths, pixel hashes and metadata summaries match
between builds. The JPEG is the existing 1920x1041 README example; the PNG is
the 4x3 smoke fixture. A generated PPM exercises the unsupported-metadata path.

Windows 11, Qt 6.11.2, GCC 16.2.0 Release; values per full read. Counts were
identical across all ten processes in each arm:

| Input | Bytes requested before | Bytes requested after | Read operations before → after |
| --- | ---: | ---: | ---: |
| JPEG, 442,271-byte file | 786,335 | 524,191 | 32 → 31 |
| PNG, 78-byte file | 312 | 234 | 8 → 6 |
| PPM fallback, 96-byte file | 384 | 384 | 8 → 8 |

The JPEG saves exactly 262,144 bytes of requested reading per open. These are
[Windows process I/O counters](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ns-winnt-io_counters),
not physical disk traffic. The OS file cache was warm. PNG's long read also
required an EOF read in the old path, so that fixture saves two operations.

Timing medians across process medians vary: JPEG 13.16 → 12.56 ms in the first
series and 15.50 → 12.26 ms in the confirmation; PNG 1.32 → 0.90 and
1.02 → 0.85 ms. The unchanged PPM control reverses direction between series
(0.75 → 1.00, then 0.57 → 0.50 ms), illustrating timing noise. No general
percentage speedup, first-paint improvement or network-drive timing is claimed.
The confirmed result is less requested I/O on the successful metadata path.

```powershell
./build/ixview_benchmarks.exe build/metadata-open.json --profile-metadata-open example.jpg -platform offscreen
```

Evidence: `build/metadata-probe-comparison.json`,
`metadata-probe-confirmation-comparison.json`, individual raw samples, and
`metadata-probe-before.exe`. The profiler branches before unrelated fixture
generation. Successful reports contain no metadata tag values or image paths.

Four real PPM cases verify plain/EXIF/XMP/IPTC-marker handling, original pixels,
loss warnings and prevention of an unapproved export when metadata cannot be
read. The existing Unicode/read-only metadata, EXIF orientation and selection
thumbnail regressions pass before and after the change (nine targeted checks
including setup/cleanup). Exiv2 may return no metadata with or without a parser
warning; export protection does not depend on a parser error being present.

All 35 Python tests and 260 Qt checks pass with required RAW/tool fixtures and
no skips: 159 main, 59 workflow, 13 quality, 12 storage, 17 extension. The core
CTest run takes 70.24 seconds. Evidence: `build/metadata-probe-validation.log`,
`metadata-probe-validation-summary.json`, and retained
`metadata-probe-*test-results.*` / `metadata-probe-core-*.log`.

The deployed portable runtime repeats all 260 Qt checks without SDK paths and
passes actual OCR/QR engines with all five OCR models plus relocated
profile/tools startup (`build/metadata-probe-portable.log`). Native Linux/macOS
confirmation still belongs to the next uploaded GitHub workflow; the local
Windows evidence does not establish a successful four-platform run.

## Retained cache data and clear boundaries — 2026-10-27

The document and thumbnail budgets now charge text, profiles and native
metadata estimates as well as pixel buffers. This prevents tiny images with
large payloads from bypassing a small cache budget. Charges can conservatively
count shared buffers more than once; they do not cap active decoder/UI/undo
memory. Cost preparation happens on insertion outside the cache mutex and is
skipped for a disabled cache. A cache hit does not walk metadata or serialize
profiles. No new file reads or content hashing are introduced by cache hits.

Cache clears advance an epoch. Decode work begun before a clear cannot restore
its old cache entry over a newer result. The reproducible interleaving uses
semaphores rather than sleeps and passes twenty consecutive checks after the
fix. Existing request generations and cooperative cancellation still apply.

The 12 MP preview profiler continues to produce exact pixels with unchanged
originals: borrowed previews measure 8.47–10.14 ms after the correction, versus
190.12–346.22 ms for decoding again in the same run. This is a cache-lifetime
and resource-accounting change, not a newly established general speedup.
Full results, original failures, native metadata/ICC cases, the maximum-budget
boundary and limitations are in [REVIEW_2026-10-27.md](REVIEW_2026-10-27.md).

The final review also corrects saved-document/Undo metadata presence without a
second production decode. All 20 local validation steps pass with 35 Python
tests and 270 Qt checks; the final portable runtime repeats all 270 checks
without SDK paths, including real recognition engines/models and relocated
storage. Native Save-as/Undo also passes separately. See the review's retained
evidence paths and remaining platform/resource limits.

## Exact integer standard sharpening — 2026-10-27

The standard sharpening operation used a general 3×3 double-precision
convolution, including four zero taps, repeated scanline lookups and per-channel
division/rounding. Its actual kernel is integer-valued: five times the center
minus the four adjacent samples. `convolve` now recognizes only that exact
kernel with divisor 1 and offset 0 and evaluates it with bounded integer
arithmetic in the same RGBA64 working format. Edge replication, center alpha,
output format conversion, profile, comments and DPI remain unchanged. Other
kernels, divisors and offsets keep the existing scalar implementation.

The shared operation serves explicit sharpening, resizing with sharpening,
default-amount batch sharpening and optional display sharpening. Ordinary
navigation with display sharpening disabled does not run this filter and has
no newly established speedup from this change. No additional image buffers,
cache budgets, dependencies or workers are introduced.

`--profile-sharpen` runs three alternating scalar/current samples per case,
using deterministic 6/24 MP RGB32 and RGBA64 inputs. The same profiler is linked
against the preceding and optimized cores. Three fresh processes per arm
produce nine production timings per case; later process pairs reverse order.
The scalar control stays in `tests/convolution_reference.h`. Pixel hashes match
between all before/after processes. Each call also checks exact pixels,
profile, comment, DPI and the unchanged original outside its timing.

Windows 11, GCC 16.2.0, Qt 6.11.2 Release. Medians include working-format
conversion, filtering and output conversion; no file I/O or GUI presentation:

| Input | Previous production path | Optimized path | Ratio |
| --- | ---: | ---: | ---: |
| 6 MP, RGB32 | 488.47 ms | 78.68 ms | 6.21× |
| 6 MP, RGBA64 | 417.78 ms | 50.37 ms | 8.29× |
| 24 MP, RGB32 | 1,763.89 ms | 628.70 ms | 2.81× |
| 24 MP, RGBA64 | 3,786.86 ms | 483.49 ms | 7.83× |

The first process pair measured 6–11×; the repeated medians above give the
more conservative 2.8–8.3× range. Timings vary substantially for large images:
the unchanged 24 MP RGB32 scalar control shifts from 1,593 to 3,636 ms between
arms, while the RGBA64 control remains near 3,500 ms. These are local workload
measurements, not a general viewer/first-open speed claim or a cross-platform
timing guarantee. No measurements run alongside compilation or validation.

```powershell
./build/ixview_benchmarks.exe build/sharpen.json --profile-sharpen -platform offscreen
```

Evidence: `build/sharpen-comparison.json`, `sharpen-before-{1,2,3}.json`,
`sharpen-after-{1,2,3}.json`, matching process logs and retained
`build/sharpen-before.exe`. Thirty new reference cases cover singleton/row/
column borders, random samples, all 65,536 channel levels, premultiplied alpha,
grayscale, floating-point inputs and fallback kernels/divisors/offsets. All
32 targeted checks including setup/cleanup pass before and after the change.

All **20 steps** of the full local validator pass: **41 Python tests** and
**300 Qt checks**, no failures/skips (159 main, 69 workflow, 43 quality,
12 storage, 17 extension). Native file/window/recognition checks and
100/125/150/200 percent DPI cases pass. Reports:
`build/sharpen-validation-summary.json`, `sharpen-validation.log`,
`sharpen-*test-results.*` and `sharpen-core-*.log`. Build logs contain no
compiler warnings. This establishes Windows evidence; native Linux/macOS
execution and timing still require their respective hosts/CI.

After deployment, all **300 portable Qt checks** repeat successfully without
SDK paths, including all required RAW/tool fixtures. Real OCR/QR processes,
all five OCR models, moved tool storage and relocated-profile startup pass.
The two native modules load and all exports resolve. Deployment still has
135 PE modules with no unresolved imports and preserves the seven tool packs.
Evidence: `build/sharpen-deploy.log`, `sharpen-installed-modules.log` and
`sharpen-portable.log`. Source and Windows archives are regenerated afterward;
README and its two existing images remain unchanged.
