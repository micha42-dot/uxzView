# Projektstatus – üxzView

**Version 0.4.4 · Stand: 24.10.2026**

C++20 und Qt 6; inspiriert von IrfanView und nomacs. Zielplattformen sind Windows 10 ab 1809, Windows 11, Linux und macOS. Fertige Pakete werden zunächst für Windows x64 vorbereitet. Native Windows-10-/Linux-/macOS-Prüfungen bleiben offen.

## Aktueller Funktionsumfang

- Bild-/Ordnernavigation, Auswahlzoom, Vollbild, Diashow, Animationen und mehrseitige Bilder; optionale Nachbarvorschauen und rechts angeordnete Befehlssuche.
- Größenänderung einschließlich exakter Pixelverdopplung, Freistellen mit eigenen Seitenverhältnissen, Drehen, Farbkorrektur und Bild-/Datei-Undo.
- Visuelle Stapelverarbeitung mit Profilen, Wasserzeichen, echter Exportvorschau und geprüftem Dateiplan; klassische Batch-Konvertierung und Umbenennung.
- Bildverwaltung mit Bewertungen, Farbmarkierungen, Metadatensuche, identischen Dateien und Ähnlichkeitskandidaten; internes Dateijournal und Papierkorb.
- Offline-Texterkennung und QR-/Barcode-Lesen für ganze Bilder oder eine Auswahl. Beide Engines und fünf OCR-Sprachen sind unter `tools` mitgeliefert, einzeln entfernbar und nur bei Bedarf aktiv. Speicherorte sind konfigurierbar.
- Getrennte Bildformat- und Funktionserweiterungen; Portabelmodus für Einstellungen, Profile, Bewertungen, Erweiterungen und Dateihistorie.
- Deutsche, englische, französische, japanische und koreanische Oberfläche; Windows-Dateizuordnung und native Build-Anleitungen für alle Zielplattformen.

Funktionen: [README](README.md). Bedienung und Einstellungen: [Tastatur](docs/KEYBOARD.md), [Einstellungen](docs/SETTINGS.md), [Portabelmodus](docs/PORTABLE.md), [Erweiterungen](docs/TOOL_EXTENSIONS.md).

## Letzte Änderungen

- 0.4.4: Abgebrochene Nachbarvorschauen starten beim Wiedereinblenden zuverlässig neu. Bereits vollständig gecachte RAWs lösen keine unnötige kleinere Vorschau mehr aus. Zufallsdiashows überspringen das gerade manuell gewählte Bild. Drei reproduzierte Fehler und ihre Regressionen: [Review 0.4.4](docs/REVIEW_0.4.4.md).

- Alle 19 Kontursymbole vereinheitlicht; Rückgängig, Einstellungen und Bildinformationen deutlicher unterscheidbar. Helle/dunkle Darstellung, DPI-Rasterung und stabile Toolbar-Zustände geprüft.
- Überholte Öffnungsaufträge werden durch neuere Entscheidungen ersetzt. EXIF-gedrehte Miniaturen behalten ihre Details. Externe Dateiänderungen während Umbenennungsdialogen werden vor der Dateioperation erneut geprüft.
- Zufallsdiashows mischen einmal pro Runde; Exporte ohne Metadaten-Nachbearbeitung vermeiden einen vollständigen zusätzlichen Ausgabepuffer.
- RAW-Vorschauen werden beim JPEG-Decodieren verkleinert; veraltete Vorschauaufträge werden vor dem Start verworfen. Lade-/Anzeigeaufbereitung und Erkennungseingaben besitzen eigenständige Kernfunktionen.
- Die Windows-Vollbildabdeckung erhält ihr vollständiges Bild vor dem Anzeigen. Native Geometrie-/Lebensdauerprüfungen bestehen; sie garantieren keine flackerfreie Darstellung auf jeder GPU.

Die Änderungshistorie steht im [Changelog](CHANGELOG.md). Befunde, Messwerte und Grenzen: [Review 24.10.](docs/REVIEW_2026-10-24.md), [Review 23.10.](docs/REVIEW_2026-10-23.md), [Performance](docs/PERFORMANCE.md).

## Prüfung

Letzte vollständige Prüfrunde auf Windows 11: **154 Funktions-, 13 Qualitäts-, 12 Speicher-, 17 Erweiterungs- und 11 Ablaufprüfungen** sowohl im Entwicklungsbuild als auch im portablen Paket ohne SDK-Pfade; insgesamt je 207 Qt-Prüfungen einschließlich Setup/Cleanup und datengetriebener Fälle. Dazu 24 Python-Tests und alle 20 Schritte des gemeinsamen Entwicklungs-/Windows-Prüflaufs, einschließlich 100/125/150/200 Prozent Skalierung.

Die vollständige Prüfrunde für 0.4.4 umfasst auch die überarbeiteten Icons; die zusätzliche native DPI-Prüfung und alle bisherigen Datei-/Navigationsregressionen bleiben Bestandteil des Ablaufs.

Fünf echte CR2-/NEF-/ARW-/PEF-/DNG-Dateien, vier unabhängige LittleCMS-Referenzen, HDR-/HEIC- und PSD-Composites sowie echte OCR-/QR-Prozesse sind enthalten. Der Windows-Codec-Bericht enthält 94 Formatschlüssel und 32 synthetische Encoder-Rundläufe. Das belegt keine vollständige Unterstützung sämtlicher Formatvarianten. Details: [Qualität](docs/QUALITY.md), [Formate](docs/FORMAT_COMPATIBILITY.md), [Plattformen](docs/PLATFORMS.md).

## Offene Grenzen und nächste Arbeit

1. Native Windows-10-/Linux-/macOS-Läufe, breitere RAW-/OCR-/Barcode-Stichproben, kalibrierte Monitorprofile, HDR-Desktop und Mehrmonitorverhalten prüfen. Wayland-Screenshots benötigen noch die Portal-Anbindung. Muttersprachliche Prüfung der Übersetzungen steht aus.
2. Laden, Navigation, Speichern und Dateiverwaltung aus dem weiterhin großen Hauptfenster schrittweise entflechten. Erst darauf aufbauend gerichtetes Vorladen mit Abbruch und ohne doppelte Decodierung ergänzen.
3. Ein gemeinsames RAM-Budget für aktive Decoder, Undo, Caches, Anzeige und Export schaffen. Die einzelnen Budgets sind keine Gesamtgrenze. Kein gekachelter Gigapixel-Decoder; bestehende Größen-/Allokationsgrenzen bleiben bestehen.
4. Automatische Wiederherstellung von `pending`-/`undoPending`-Dateioperationen nach einem Absturz ergänzen. Revisionstest und Dateisystemmutation sind gegenüber anderen Programmen nicht atomar. Große JSON-Kataloge und synchrone Formatpaket-Installation benötigen weitere Arbeit.
5. Paketgröße bleibt erheblich: rund 179,6 MB entpackt, davon etwa 24 MB Erkennungswerkzeuge. Weitere Einsparungen erfordern geprüfte optionale Formatpakete oder einen kleineren Qt-/ICU-Build.

Weitere technische Einschränkungen: Export speichert nur die aktuelle Seite bzw. den aktuellen Frame; kein Animations-/Mehrseitenencoder. ICNS-Schreiben bleibt wegen stiller Größenänderungen gesperrt. PSD zeigt gespeicherte Composites; Ebenen/PSB/CMYK sind nicht umfassend geprüft. MakerNotes, große Metadaten und HDR-Bearbeitung sind nicht vollständig abgesichert. JPEG-Verlustfreioperationen unterstützen EXIF-Ausrichtung ungleich 1 noch nicht. Einzelne Decoder und Operationen sind nicht sofort abbrechbar. Ein vollständiger manueller Druck-/Zwischenablage-/Drag-and-drop-Durchlauf und sämtliche Referenzdialoge sind nicht nachgewiesen.

Eine große Effektgalerie, 8BF, Audio/Video und KI-Dienste gehören nicht zum aktuellen Kernumfang. Weitere optionale Funktionen und frühere Referenzbeobachtungen stehen unter [Erweiterungsideen](docs/EXTENSION_IDEAS.md) und [Referenz](reference/REFERENCE.md); sie sind keine fertig implementierten Zusagen.

## Entwicklung und Auslieferung

[Architektur](docs/ARCHITECTURE.md) beschreibt Zuständigkeiten, Threads und Speicherverträge; [Entwicklung](docs/DEVELOPMENT.md) die reproduzierbaren Prüfabläufe. `CMakeLists.txt` ist die maßgebliche Versionsquelle. Der geprüfte Wartungsstand trägt 0.4.4; bestehende Profile und Werkzeugpakete bleiben kompatibel.

Die gemeinsamen Ausgaben liegen unter `dist/uxzView`, `dist/uxzView-win64.zip` und `dist/uxzView-source.zip`; Quellhinweise und Prüfsummen liegen ebenfalls direkt unter `dist`. Die Release-Paketierung prüft eigene Binärdateien und Drittanbieter-Quellen und schließt private Profile aus. Werkzeugpakete unter `dist/tools` sind eigenständige optionale Downloads. Kein zusätzliches Release- oder GitHub-Quellverzeichnis. [Release-Anleitung](docs/RELEASING.md), [GitHub-Vorbereitung](docs/GITHUB.md).
