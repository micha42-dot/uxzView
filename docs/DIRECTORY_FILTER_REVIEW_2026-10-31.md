# Ordnerfilter und Navigation · 31.10.2026

Die Ordnersuche prüfte jeden Dateinamen gegen alle aktivierten Formatmuster.
Mit dem lokalen Formatumfang sind das 99 Muster wie `*.png`, `*.jpg` und
`*.svgz`. Diese Arbeit läuft bereits im Hintergrund, verzögert aber die
Übernahme der fertigen Liste und damit die Navigation in einem neu geöffneten
oder erneut eingelesenen Ordner.

## Änderung

`src/core/directory_index.cpp` erkennt einfache ASCII-Formatendungen und baut
einmal pro Scan eine kleine Hash-Menge auf. Jeder Dateiname liefert seine letzte
Endung zur direkten Prüfung. Erst passende Dateien erhalten Sortierschlüssel,
absolute Pfade und gegebenenfalls Zeit-/Größenattribute.

Leere Filterlisten, leere Endungen, zusammengesetzte Endungen, Suchmuster und
Unicode-Formatnamen behalten Qts bisherigen Filterweg. Unicode-Endungen in
Dateinamen müssen ebenfalls Qts Fallvergleich beibehalten. Versteckte Dateien,
Verzeichnisse, Sortierung, Positionsindex, neueste Datei und Abbruchsemantik
werden nicht neu definiert. Es kommt kein dauerhafter Cache und kein zusätzlicher
Worker hinzu; die Menge wird nach dem Scan freigegeben.

## Reproduzierbarer Vergleich

Der optionale Modus in `tests/bench_ixview.cpp` erzeugt 10.000 kleine Dateien:
je 2.500 mit `PNG`, `jpg`, `svgz` und `txt`. Die Inhalte sind kleine Testbytes;
hier werden Dateinamen gefiltert, keine Bilder dekodiert. Er misst die vollständige
natürliche Sortierung samt Positionsindex, mit einem beziehungsweise allen
aktivierten Formaten sowie mit zusätzlicher Suche nach der neuesten Datei.
Jedes Szenario läuft dreimal. Ein SHA-256 über die geordneten Dateinamen prüft,
dass alle Durchläufe und beide Implementierungen dieselben Ergebnisse liefern.
Erzeugen/Löschen der Fixture und Bilddekodierung liegen außerhalb der Messung.

```text
build/ixview_benchmarks.exe build/directory-filter-after-final.json --profile-directory-filters -platform offscreen
```

Die Ausgangsmessung verwendet denselben Profiler mit der bisherigen
Produktimplementierung. Rohdaten: `build/directory-filter-before.json` und
`build/directory-filter-after-final.json`. Der erste Optimierungsversuch ist
zusätzlich als `build/directory-filter-after.json` erhalten.

Median aus jeweils drei Messungen:

| Szenario | Gefundene Dateien | Bisher | Jetzt |
| --- | ---: | ---: | ---: |
| Eine Endung, ohne Suche nach neuester Datei | 2.500 | 74,16 ms | 42,21 ms |
| Alle 99 Endungen, ohne Suche nach neuester Datei | 7.500 | 1.144,34 ms | 64,86 ms |
| Alle 99 Endungen, mit Suche nach neuester Datei | 7.500 | 2.366,77 ms | 287,56 ms |

Dateianzahl und SHA-256 der geordneten Namen stimmen vor/nach der Änderung in
allen drei Szenarien überein. Die endgültige Implementierung enthält bereits
die Unicode-Behandlung; deren Kosten sind in den Messwerten enthalten.

Das sind Komponentenmessungen auf Windows 11 mit Qt 6.11.2 und warmem
Dateisystemcache. Sie messen keine Kaltstarts, Netzlaufwerke, Bilddekodierung,
RAW-Entwicklung oder komplette End-to-end-Bildwechsel. Der aktuelle Ordner
wird beim normalen Blättern bereits wiederverwendet; dort wird nicht für jedes
Bild neu gescannt. Die Beschleunigung betrifft den tatsächlichen Neuaufbau der
Liste, nicht pauschal jeden Bildwechsel oder die erste sichtbare Bildanzeige.

## Verlässlichkeit

Acht neue datengetriebene Fälle vergleichen die gefundenen Dateien mit Qt:
einfache/mischgeschriebene Endungen, Duplikate, zusammengesetzte Endungen,
Suchmuster, Unicode, eine leere Endung und eine leere Filterliste. Die Fixture
enthält versteckte Dateien, ein Verzeichnis mit Bildendung, Namen ohne Punkt,
mehrere Punkte, ähnliche falsche Endungen und das Unicode-Zeichen langes s.
Positionsindex und neueste Datei werden ebenfalls geprüft. Die bestehenden
Fälle prüfen alle vier Sortierarten in beiden Richtungen und Abbruch.

Eine reine Kleinschreibungsprüfung der ersten Optimierung übersah `ſvgz`, das Qt
wie `svgz` behandelt. Zwei der neuen Datenfälle scheiterten damit. Der endgültige
Pfad behält für nicht-ASCII-Dateiendungen `QDir::match` bei; der ursprüngliche
Fehlversuch bleibt in `build/directory-filter-unicode-before.txt` erhalten.

Der vollständige Windows-Entwicklungsbuild und das portable Paket bestehen
jeweils **473 Qt-Fälle** der acht Suiten; alle **45 Python-Tests** bestehen.
Die Portable-Prüfung läuft ohne SDK im Suchpfad und prüft zusätzlich die
QR-Engine, OCR in fünf Sprachen und den verschobenen Portabelordner.

Belege: `build/directory-filter-tests.txt`,
`build/directory-filter-validation-summary.json`,
`build/directory-filter-portable.log` und
`build/directory-filter-archives.log`. Die vollständige native Desktopmatrix
wurde für diese Core-Änderung nicht erneut ausgeführt. Native Linux-/macOS-
Ausführung, Windows 10 und ein gemeinsames Prozess-RAM-Budget bleiben offene
Aufgaben; dieser Patch löst sie nicht.
