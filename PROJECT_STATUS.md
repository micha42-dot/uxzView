# Projektstatus – üxzView

**Version 0.5.0 · Stand: 29.10.2026**

C++20 und Qt 6; inspiriert von IrfanView und nomacs. Zielplattformen sind Windows 10 ab 1809, Windows 11, Linux und macOS. Fertige Pakete werden zunächst für Windows x64 vorbereitet. Native Windows-10-/Linux-/macOS-Prüfungen bleiben offen.

## Aktueller Funktionsumfang

- Bild-/Ordnernavigation, Auswahlzoom, Vollbild, Diashow, Animationen und mehrseitige Bilder; optionale Nachbarvorschauen und rechts angeordnete Befehlssuche.
- Größenänderung einschließlich exakter Pixelverdopplung, Freistellen mit eigenen Seitenverhältnissen, Drehen, Farbkorrektur und Bild-Undo.
- Visuelle Stapelverarbeitung mit Profilen, Wasserzeichen, direktem Start, automatischer Zielprüfung, Such-/Formatfiltern und optionalem CSV-Protokoll; klassische Batch-Konvertierung und Umbenennung.
- Bildverwaltung mit Bewertungen, Farbmarkierungen, Metadatensuche, identischen Dateien und Ähnlichkeitskandidaten; Systempapierkorb.
- Offline-Texterkennung und QR-/Barcode-Lesen für ganze Bilder oder eine Auswahl. Beide Engines und fünf OCR-Sprachen sind unter `tools` mitgeliefert, einzeln entfernbar und nur bei Bedarf aktiv. Speicherorte sind konfigurierbar.
- Getrennte Bildformat- und Funktionserweiterungen; Portabelmodus für Einstellungen, Profile, Bewertungen und Erweiterungen.
- Deutsche, englische, französische, japanische und koreanische Oberfläche; Windows-Dateizuordnung und native Build-Anleitungen für alle Zielplattformen.

Funktionen: [README](README.md). Bedienung und Einstellungen: [Tastatur](docs/KEYBOARD.md), [Einstellungen](docs/SETTINGS.md), [Portabelmodus](docs/PORTABLE.md), [Erweiterungen](docs/TOOL_EXTENSIONS.md).

## Letzte Änderungen

- Die Bildfläche bleibt im Fenster und nach Vollbildwechseln rahmenlos;
  der Trennstrich über den Bildinformationen bleibt erhalten.

- Menü-/Einstellungsprüfung: kompakteres, beim Kategorienwechsel stabiles Fenster;
  neun Bereiche, Vorschauoptionen rahmenlos unter Anzeige, passende Feldbreiten.
  Farbkorrektur und Drehen öffnen kleiner. Sechs Menütexte sind in fünf Sprachen
  gekürzt; klassische Stapelumbenennung startet im passenden Modus.
  428 Qt-Fälle und 45 Python-Tests bestehen; native Oberflächenprüfungen umfassen
  vier Skalierungen. [Menü-/Einstellungsbericht](docs/MENU_SETTINGS_REVIEW_2026-10-29.md).

## Frühere Entwicklungsstände

- Speicherprüfung: Batch-Aufträge geben verkleinerte Originalpuffer früher frei;
  Vorschauen halten das volle Ergebnis nicht beim erneuten Decodieren fest.
  Cache-Invalidierung nutzt eine temporäre Hash-Menge statt wiederholter
  Listensuchen. 418 Qt-Fälle im Build und portablen Paket sowie 45 Python-Tests
  bestehen. [Speicherbericht](docs/MEMORY_REVIEW_2026-10-29.md).

- 0.5.0 entfernt das Dateijournal und Dateioperationen-Undo vollständig.
  Löschen nutzt den Systempapierkorb; Bildbearbeitungs-Undo bleibt erhalten.
  Umbenennen benötigt keinen vollständigen Dateihash und schreibt keine
  Verlaufsdateien. Batch-Ersetzungen verwenden direkt atomare QSaveFile-Ausgabe
  mit Ziel-/Quellprüfung und Abbruch vor Commit. CSV-Konvertierungsberichte
  bleiben optional verfügbar. Build und portable Ausgabe bestehen jeweils
  417 Qt-Fälle ohne Auslassungen; 45 Python-Tests und alle 33 lokalen Prüfschritte
  bestehen. Native Windows-Oberflächentests laufen bei 100/125/150/200 Prozent.
  QR, alle fünf OCR-Sprachen und der verschobene Portabelmodus bestehen ohne
  SDK-Pfade. [Prüfbericht 0.5.0](docs/REVIEW_0.5.0.md).
  Ältere Einträge unten dokumentieren frühere Stände.

- Die 0.5.0-Nachprüfung korrigiert Bild- und Bewertungszuordnung bei wiederverwendeten
  Namen in Stapelumbenennungen. Unveränderte Pixel sowie Bild-Undo/Redo bleiben
  erhalten; Dateirevisionen verhindern diese Übernahme nach externen Änderungen.
  Unbeteiligte Caches bleiben bestehen, Kataloge ohne betroffenen Eintrag werden
  nicht neu geschrieben. Native Papierkorbaktionen prüfen auch Änderungen während
  der Bestätigung. Das Seitenverhältnis-Menü enthält nur Vorgaben/eigene Formate;
  normales Freistellen bleibt an eine Auswahl gebunden. Reine Anzeigeauffrischungen
  behalten Rahmen und Verhältnis. Details und Grenzen im
  [Prüfbericht](docs/REVIEW_0.5.0.md#follow-up-after-removing-the-journal).



- Noch unveröffentlicht: Direkte Dateioperationen behalten unveränderte
  Nachbarbilder, Vorschaubilder und APNG-Sitzungen im Cache. Bereits ausgeführte
  Dateiänderungen werden trotz nachträglicher Journalfehler korrekt angezeigt;
  zusätzliche Quell-/Zielrevisionen erkennen Änderungen durch andere Programme.
  438 Qt-Fälle im Build und in der portablen Ausgabe, 45 Python-Tests und alle
  33 lokalen Prüfschritte bestehen. Der Umbenennungs-Regressionsfall vermeidet
  einen zusätzlichen Decode des unveränderten Nachbarbilds.
  [Datei-/Cache-Nachprüfung](docs/UI_UX_REVIEW_2026-10-29.md#file-operation-follow-up).

- Noch unveröffentlicht: Löschen nutzt den Papierkorb des Betriebssystems;
  Wiederherstellen erfolgt dort. Die untere Bildinfo bleibt bei Befehls-Hover
  sichtbar. Das Histogramm skaliert mit Fenster, DPI und Theme; die
  Erweiterungsaktionen sind für kleinere Fenster neu gruppiert. Lesbarere
  deaktivierte Texte, zugängliche Feldnamen, Label-Verknüpfungen und gemeinsame
  Dialogabstände. 423 Qt-Fälle im Build und in der portablen Ausgabe,
  45 Python-Tests und 33 lokale Prüfschritte bestehen, einschließlich nativer
  Windows-Fenstertests bei vier Skalierungen. [UI/UX-Bericht](docs/UI_UX_REVIEW_2026-10-29.md).


- Noch unveröffentlicht: Abgeschlossene Bildaufträge halten nach Paste/New/Delete
  keine verwaisten Original-/Anzeigepixel mehr. Im kontrollierten 24-MP-Test sinkt
  der private Speicher nach Ersetzen von 131,06 auf 39,49 MiB; 30 weitere Zyklen
  bleiben bei 39–40 MiB. Fensterzerstörung wartet auf eigene Bildarbeit.
  `DocumentHistory` besitzt Undo/Redo einschließlich gemeinsamer Speicherrechnung;
  verlustfreies JPEG prüft Quelle/Ziel, ZIP begrenzt implizite Verzeichnisse.
  45 Python- und 391 Qt-Fälle, alle 29 lokalen Prüfschritte und die portable
  Ausgabe bestehen. Reduzierter Windows-Build und gezielte GCC-Analyse bestehen;
  native Windows-10-/Linux-/macOS-Prüfung und globale RAM-Koordination bleiben offen.
  [Architektur-/Ressourcenbericht mit Befunden, Messungen und offenen Risiken](docs/ARCHITECTURE_REVIEW_2026-10-28.md).

- Noch unveröffentlicht: ZIP-/CBZ-Bildsammlungen über ein separates KArchive-
  Bildformat-Plugin lesen, natürlich sortieren, durchblättern, als Vorschau
  anzeigen und einzelne Seiten exportieren, ohne Dateien zu entpacken.
  Das Windows-Paket nutzt das vorhandene KArchive; neue Decoder-DLL ca. 105 KB.
  PDF, CBR/RAR und JPEG XR bleiben offen. Archivvarianten und Grenzen stehen in
  `docs/FORMAT_COMPATIBILITY.md`; Linux/macOS bauen ohne KArchive weiterhin
  ohne diese optionale Funktion. 45 Python- und 379 Qt-Fälle bestehen lokal;
  die 14 gezielten Archivfälle bestehen auch mit nativen Windows-Fenstern.
  Die portable Ausgabe besteht dieselben 379 Qt-Fälle ohne SDK-Pfade.

- Noch unveröffentlicht: Anzeigeoptionen zeigen genau eine Auswahlmarkierung;
  Fenster-/Bildschirmvariante bleibt beim Neustart erhalten. Alle Menüs nutzen
  einheitliche Häkchen auch für die exklusive Auswahl. Die Markierung folgt
  manuellem Zoom und der Fensterrückkehr aus Vollbild, auch bei blockierten Aktionen.
  Alle 28 lokalen Prüfschritte mit 43 Python- und 367 Qt-Fällen bestehen;
  die portable Ausgabe besteht dieselben 367 Qt-Fälle ohne SDK-Pfade.

- Noch unveröffentlicht: Änderungen der Fenstereinpassung während Vollbild gelten
  beim Zurückwechseln. Ältere Fenster überschreiben beim Schließen keine neuere
  Einpassung; OK synchronisiert einen veralteten lokalen Fenstermodus. Quer- und
  Hochformatfälle mit 3331 × 2244 und 2594 × 3486 Pixeln sind als Regression erfasst.
  Alle 28 lokalen Prüfschritte bestehen mit 43 Python- und 363 Qt-Fällen,
  einschließlich der Einpassungsprüfung bei vier nativen Windows-DPI-Skalierungen.
  Auch die portable Fassung besteht alle 363 Qt-Fälle ohne SDK-Pfade.

- Noch unveröffentlicht: Automatische Fenstereinpassung bleibt beim Bildwechsel
  auch mit aktivierter Zoomsperre erhalten. Ein alter Zahlenwert schaltet Panoramen
  nicht mehr auf manuellen Zoom mit Scrollbereichen um. Bewusst gesetzter manueller
  Zoom bleibt weiterhin sperrbar; Tests decken auch den ersten Bildaufruf ab.

- Noch unveröffentlicht: Unterbrochene Dateivorgänge lassen sich im Dateijournal
  anhand von Prüfsummen abgleichen; fehlende Ziele aus geprüften Sicherungen
  wiederherstellen. Undo speichert den Ablagepfad vor dem Verschieben und setzt
  bei Wiederholung denselben Vorgang fort. Mehrdeutige/geänderte Dateien bleiben
  erhalten. Fortschrittsmeldungen großer Stapel werden pro Lauf zusammengefasst.
  Alle 24 lokalen Prüfschritte bestehen mit 43 Python- und 356 Qt-Fällen.

- Noch unveröffentlicht: Linux-Paketierung prüft die Build-Module mit vorhandenem
  SDK-Suchpfad, damit auch nur optional geladene Abhängigkeiten wie Qt Network
  mitgeliefert werden. Neuer nativer ELF-/Umzugstest im Linux-CI-Lauf;
  Bestätigung durch den nächsten GitHub-Lauf steht aus.

- Noch unveröffentlicht: Stapelverarbeitung ohne äußere Scrollleiste, mit getrennten
  Reitern für Verarbeitung und Ergebnis. Schlankes Ergebnismodell, ausgelagerte
  Schrittbearbeitung, lineare Dateiduplikatprüfung und CSV-Export im Hintergrund.
  Übersprungene Stapel laden keine Wasserzeichen und starten keine Decoderthreads.
  Alle 24 lokalen Prüfschritte mit 43 Python- und 356 Qt-Fällen bestehen,
  ebenso die portable Fassung mit 356 Qt-Fällen ohne SDK-Pfade.
  [Kritische Prüfung und verbleibende Prioritäten](docs/REVIEW_2026-10-28.md).

- Noch unveröffentlicht: Proportionale Pixelmaße aktualisieren sich beim Tippen;
  Standarddimensionen stehen in einem einzigen Dropdown. Die Stapelverarbeitung
  startet mit leerer Kette und prüft Ziele automatisch beim Start, hat Such-/Formatfilter
  und optionales CSV-Protokoll mit tatsächlichen Größen. Vollbild erhält eigene
  Anpassungsmodi und eine Bildschirmwahl. Rückkehr ins Fenster verwirft manuellen
  Zoom/Pan; die Trennlinie über Dateiinfos ist wieder sichtbar.

- Noch unveröffentlicht: Sprachwechsel ohne Neustart nach „OK“ in Einstellungen
  → Oberfläche. Menüs, Werkzeugtipps und Befehlssuche werden direkt aktualisiert;
  Bild, Bearbeitungen, Zoom, Auswahl und Undo bleiben erhalten. Unveränderliche
  Übersetzungstabellen erlauben parallele Zugriffe aus Hintergrundaufträgen.

- Noch unveröffentlicht: Batch-Dateiplanung als eigenes Kernmodul, Abbruch auch
  während Prüfsummen und frühzeitige Zielprüfung. Übersprungene Ziele werden
  nicht mehr vollständig gelesen; im dokumentierten 128-MiB-Test sinkt die
  Planungszeit von 927 auf 26 ms. Alte Vorschauen/Pläne werden bei Änderungen
  oder Abbruch verworfen. Die Architektur dokumentiert die nächsten klaren
  Zuständigkeitsgrenzen. [Wartbarkeitsprüfung](docs/REVIEW_2026-10-27.md).

- Noch unveröffentlicht: Hell-, Dunkel- und Systemdesign unter Einstellungen → Oberfläche;
  Menü- und Symbolleiste teilen dieselbe Grundfarbe. Einheitliche Dialogabstände,
  skalierbare Größen-/Farb-/Rotationsdialoge und erreichbare Dialogschaltflächen.
  Reine Oberflächenänderungen behalten Bild, Auswahl und Zoom ohne erneute
  Bildberechnung oder Ordnerindizierung. Technischer Aufbau: [UI_STYLE](docs/UI_STYLE.md).

- Noch unveröffentlicht: Feinere Icons mit 1,25 statt 1,65 Einheiten Strichstärke, dezente Trennlinien, flaches Zoomfeld mit sichtbarem Auswahlpfeil und durchgehende Statuszeile ohne versenkte Kästchen. Symbolformen, Klickflächen, Suche rechts und Tastaturbedienung bleiben erhalten. Größen 20/24/28/32 px sowie vier DPI-Skalierungen geprüft; [Gestaltungsregeln](docs/UI_STYLE.md).

- Noch unveröffentlicht: CI-Lauf `8878b46` zeigt denselben Pfadtestfehler auf Windows und beiden Macs sowie einen falschen Linux-Anzeigetreiber im Pakettest. Erwartete Pfade werden jetzt kanonisch verglichen; Linux prüft XCB über Xvfb. Alle 42 lokalen Python-Tests einschließlich acht simulierter Shell-Abläufe bestehen. Linux hat im veröffentlichten Lauf bereits gebaut und alle fünf CTest-Suiten bestanden; die korrigierten vollständigen Jobs benötigen einen neuen Lauf. [CI-Nachprüfung](docs/CI_REPAIR_2026-10-27.md).

- Noch unveröffentlicht: Standard-Nachschärfen nutzt eine pixelgleiche Ganzzahlberechnung statt der allgemeinen Faltung. Davon profitieren Bearbeitung, Größenänderung mit Nachschärfen, Standard-Stapelverarbeitung und aktivierte Anzeigeschärfung. Bei getesteten 6/24-MP-Bildern 2,8–8,3-fach schneller; kein zusätzlicher Bildpuffer oder neue Bibliothek. Alle 20 lokalen Prüfschritte mit 41 Python- und 300 Qt-Prüfungen bestehen. [Messung und Grenzen](docs/PERFORMANCE.md#exact-integer-standard-sharpening--2026-10-27).

- Noch unveröffentlicht: Build-Prüfung aus frischem Source-Archiv für Vollausbau und reduzierte Fassung; CI und Release verlangen jetzt Exiv2, LibRaw und Qt Network. Modul-Ladeprüfungen, serielle CTest-Ausführung mit Zeitlimit und zusätzliche Installations-/Paketprotokolle ergänzt. Lokal bestehen 41 Python-Tests und alle 270 Qt-Kernprüfungen mit vollständigen Testdaten; der aktuelle Linux-/macOS-Stand braucht weiterhin einen neuen GitHub-Lauf. [Build-Prüfung vom 27.10.](docs/BUILD_REVIEW_2026-10-27.md).

- Noch unveröffentlicht: Zwei Cachefehler reproduziert und korrigiert: alte Ladeabschlüsse dürfen nach dem Leeren keine neueren Einträge überschreiben; Speicherbudgets berücksichtigen jetzt auch Kommentare, Texte, ICC-Profile und native Metadaten. Inhalte bleiben erhalten, kleine Einträge werden weiter gecacht. Alle 20 lokalen Windows-Prüfschritte mit 35 Python- und 270 Qt-Prüfungen bestehen; erzwungene Überschneidungen zusätzlich 20-mal ohne Wartezeiten. [Prüfung vom 27.10.](docs/REVIEW_2026-10-27.md).

- Noch unveröffentlicht: Speichern und Undo übernehmen die Metadatenkennzeichnung des tatsächlich geschriebenen Dateistands. Ein gültiges, aber leeres Metadatenobjekt löst später keine falsche EXIF/IPTC/XMP-Verlustwarnung mehr aus.

- Noch unveröffentlicht: Die zusätzliche Metadaten-Signatursuche entfällt bei erfolgreichem Exiv2-Ergebnis. Beim Test-JPEG werden pro vollständigem Öffnen 256 KiB weniger Daten angefordert; der Rückfall samt Exportwarnungen bleibt erhalten. Kein allgemeiner Geschwindigkeits-Prozentsatz wird aus den schwankenden Zeiten abgeleitet. Lokal bestehen 35 Python- und 260 Qt-Prüfungen. [Messung](docs/PERFORMANCE.md#remove-the-redundant-metadata-header-probe--2026-10-26).

- Noch unveröffentlicht: Im GitHub-Lauf `c795e2a` kompilieren alle vier Ziele; beide macOS-Testsuitenläufe bestehen. Folgefehler in Windows-/Linux-Testdaten und im macOS-Pakettest sind lokal korrigiert: eindeutige Vorschau-Dateirevision, gepufferte Daten vor dem Test-Zeitstempel schreiben und Cocoa statt Offscreen beim Paketstart. Lokal bestehen 35 Python- und 256 Qt-Prüfungen; 140 zusätzliche Vorschaufälle bestehen ohne neue Wartezeiten. Ein neuer GitHub-Lauf bleibt erforderlich. [CI-Nachprüfung](docs/CI_REPAIR_2026-10-26.md).

- Noch unveröffentlicht: PNG-/BMP-Vorschaubilder nutzen unveränderte Originale aus dem vorhandenen RAM-Cache. Gemessen bei 12 MP: 5–10 statt 154–322 ms pro Miniatur bei bereits geladenem Original, ohne neue Bibliotheken oder zusätzliches Cache-Budget. JPEG/RAW/Animationen behalten ihre Decoderpfade. Alle 20 lokalen Windows-Prüfschritte mit 35 Python- und 256 Qt-Prüfungen bestehen. [Messung und Grenzen](docs/PERFORMANCE.md#avoid-repeat-decoding-for-cached-pngbmp-previews--2026-10-26).

- Noch unveröffentlicht: Die Statusleiste verwendet den Zeitstempel der geladenen Dateirevision ohne erneute Dateisystemabfragen beim Zoomen/Auswählen. Speichern und Umbenennen liefern Dateistand und Attribute aus dem Worker; Undo behält den Zielbezug. Neue begrenzte Lade-Diagnosen sichern auch bei Prozessfehlern Teilprotokolle und Checkpoints. Alle 20 Windows-Prüfschritte mit 35 Python- und 248 Qt-Prüfungen bestehen, einschließlich vier DPI-Skalierungen. [Prüfung vom 26.10.](docs/REVIEW_2026-10-26.md).

- Noch unveröffentlicht: Die Formatliste für die erste Ordnerindizierung wird im Worker statt vor der Bildanzeige im GUI-Thread ermittelt. Verlauf und letzter Ordner werden gemeinsam und nur bei Änderungen geschrieben. Gemessene erste Zeichnung im Windows-Fenster: PNG 141 → 69 ms, JPEG 152 → 84 ms (je drei Vergleichsläufe, begrenzte Stichprobe). Alle 30 Python- und 246 Qt-Prüfungen des lokalen Kernlaufs bestehen. README-Bilder bleiben auch in Export und mitgelieferter Dokumentation erhalten. [Messung und Grenzen](docs/PERFORMANCE.md#first-image-in-an-already-running-window-2026-10-25).

- Noch unveröffentlicht: Python-Release-Tests ohne vorhandenes Ausgabeverzeichnis, Exiv2-0.27-Zeigerprüfung und plattformfeste Dialogautomatisierung korrigiert; vollständige CI-Testprotokolle ergänzt. Lokal bestehen 29 Python-Tests und 245 Qt-Prüfungen. Die Bestätigung durch Linux-/macOS-CI und die konkrete Ursache des bisherigen macOS-Workflow-Fehlers bleiben offen. [CI-Prüfung](docs/CI_REPAIR_2026-10-25.md).

- 0.4.8 hält die Befehlsliste beim Tippen stabil und aktualisiert ihre Zustände direkt. Leeres Enter löst keinen alten Treffer mehr aus. Bildverwaltung und Vorschaumarkierung sind gestrafft, Farbnamen übersetzt; abgebrochene Indizierungen melden keinen Erfolg. [Effizienz- und Stilprüfung](docs/REVIEW_0.4.8.md).

- 0.4.7 bricht überholte Ordnersuchen ab und behält nur die neueste wartende Anfrage. Der neue DirectoryIndexService besitzt Worker, Abbruch und Lebensdauer; das Hauptfenster übernimmt weiterhin die fertigen Listen und Navigation. [Prüfung der Ordneraufträge](docs/REVIEW_0.4.7.md).

- 0.4.6 schützt Speicherziele mit Dateirevisionen vor und nach dem Encoderlauf, verweigert unbekannte Journal-Undo-Typen und prüft Batch-Quellen nach dem Lesen sowie vor der Ausgabe. DPI-Grenzen und Speicherabschätzungen der Größenänderung sind abgesichert. Leertaste/Rücktaste besitzen einen direkten Navigationspfad. [Prüfung der Datenpfade](docs/REVIEW_0.4.6.md).
- 0.4.5 korrigiert widersprüchliche Größen-/DPI-Eingaben, das Speichern von Vorgaben trotz Abbrechen, Dezimaleingaben und Rotationsoptionen. Bildschirm-Vorgaben greifen nicht mehr mit negativen Button-IDs auf die Größentabelle zu. Filterbezeichnungen und Profilbearbeitung sind vereinheitlicht. [Konsistenzprüfung](docs/REVIEW_0.4.5.md).
- Pixel-Art steht direkt im Dropdown der Größenänderungsfilter. Die koreanische Oberfläche verwendet eine passende UI-Schrift; kleine koreanische und japanische Dialogtexte bleiben mindestens 9 Punkt groß.
- 0.4.4: Abgebrochene Nachbarvorschauen starten beim Wiedereinblenden zuverlässig neu. Bereits vollständig gecachte RAWs lösen keine unnötige kleinere Vorschau mehr aus. Zufallsdiashows überspringen das gerade manuell gewählte Bild. Drei reproduzierte Fehler und ihre Regressionen: [Review 0.4.4](docs/REVIEW_0.4.4.md).

- Alle 19 Kontursymbole vereinheitlicht; Rückgängig, Einstellungen und Bildinformationen deutlicher unterscheidbar. Helle/dunkle Darstellung, DPI-Rasterung und stabile Toolbar-Zustände geprüft.
- Überholte Öffnungsaufträge werden durch neuere Entscheidungen ersetzt. EXIF-gedrehte Miniaturen behalten ihre Details. Externe Dateiänderungen während Umbenennungsdialogen werden vor der Dateioperation erneut geprüft.
- Zufallsdiashows mischen einmal pro Runde; Exporte ohne Metadaten-Nachbearbeitung vermeiden einen vollständigen zusätzlichen Ausgabepuffer.
- RAW-Vorschauen werden beim JPEG-Decodieren verkleinert; veraltete Vorschauaufträge werden vor dem Start verworfen. Lade-/Anzeigeaufbereitung und Erkennungseingaben besitzen eigenständige Kernfunktionen.
- Die Windows-Vollbildabdeckung erhält ihr vollständiges Bild vor dem Anzeigen. Native Geometrie-/Lebensdauerprüfungen bestehen; sie garantieren keine flackerfreie Darstellung auf jeder GPU.

Die Änderungshistorie steht im [Changelog](CHANGELOG.md). Befunde, Messwerte und Grenzen: [Review 24.10.](docs/REVIEW_2026-10-24.md), [Review 23.10.](docs/REVIEW_2026-10-23.md), [Performance](docs/PERFORMANCE.md).

## Prüfung

Aktueller lokaler Kandidat auf Windows 11: **155 Funktions-, 13 Qualitäts-, 12 Speicher-, 17 Erweiterungs- und 51 Ablaufprüfungen** sowohl im Entwicklungsbuild als auch im portablen Paket ohne SDK-Pfade; insgesamt je 248 Qt-Prüfungen einschließlich Setup/Cleanup und datengetriebener Fälle. Dazu 35 Python-Tests und alle 20 Schritte des Entwicklungs-/Windows-Prüflaufs einschließlich 100/125/150/200 Prozent Skalierung. Keine Fehler oder übersprungenen Fälle. Erfolgreiche Fensterprüfungen auf diesem Rechner garantieren keine flackerfreie Ausgabe auf jeder GPU.

Die aktuelle SDK-freie portable Fassung besteht auch echte OCR-/QR-Prozesse mit allen fünf Modellen und den Profil-/Werkzeugumzug. Die bisherigen Datei-/Navigationsregressionen bleiben Bestandteil des Ablaufs. Der frühere einzelne native Mess-Timeout bleibt ungeklärt; sechs neue Diagnoseprozesse liefen erfolgreich durch und erhalten künftig bei Fehlern Checkpoints und Teilprotokolle.

Fünf echte CR2-/NEF-/ARW-/PEF-/DNG-Dateien, vier unabhängige LittleCMS-Referenzen, HDR-/HEIC- und PSD-Composites sowie echte OCR-/QR-Prozesse sind enthalten. Der Windows-Codec-Bericht enthält 94 Formatschlüssel und 32 synthetische Encoder-Rundläufe. Das belegt keine vollständige Unterstützung sämtlicher Formatvarianten. Details: [Qualität](docs/QUALITY.md), [Formate](docs/FORMAT_COMPATIBILITY.md), [Plattformen](docs/PLATFORMS.md).

## Offene Grenzen und nächste Arbeit

1. Native Windows-10-/Linux-/macOS-Läufe, breitere RAW-/OCR-/Barcode-Stichproben, kalibrierte Monitorprofile, HDR-Desktop und Mehrmonitorverhalten prüfen. Wayland-Screenshots benötigen noch die Portal-Anbindung. Muttersprachliche Prüfung der Übersetzungen steht aus.
2. Die Auftragssteuerung der Ordnerindizierung ist ausgelagert. Laden, Navigation, Speichern und Dateiverwaltung aus dem weiterhin großen Hauptfenster weiter schrittweise entflechten. Erst darauf aufbauend gerichtetes Vorladen mit Abbruch und ohne doppelte Decodierung ergänzen.
3. Ein gemeinsames RAM-Budget für aktive Decoder, Undo, Caches, Anzeige und Export schaffen. Die einzelnen Budgets sind keine Gesamtgrenze. Kein gekachelter Gigapixel-Decoder; bestehende Größen-/Allokationsgrenzen bleiben bestehen.
4. Automatische Wiederherstellung von `pending`-/`undoPending`-Dateioperationen nach einem Absturz ergänzen. Revisionstest und Dateisystemmutation sind gegenüber anderen Programmen nicht atomar. Große JSON-Kataloge und synchrone Formatpaket-Installation benötigen weitere Arbeit.
5. Paketgröße bleibt erheblich: rund 179,3 MB entpackt, davon etwa 24 MB Erkennungswerkzeuge. Weitere Einsparungen erfordern geprüfte optionale Formatpakete oder einen kleineren Qt-/ICU-Build.

Weitere technische Einschränkungen: Export speichert nur die aktuelle Seite bzw. den aktuellen Frame; kein Animations-/Mehrseitenencoder. ICNS-Schreiben bleibt wegen stiller Größenänderungen gesperrt. PSD zeigt gespeicherte Composites; Ebenen/PSB/CMYK sind nicht umfassend geprüft. MakerNotes, große Metadaten und HDR-Bearbeitung sind nicht vollständig abgesichert. JPEG-Verlustfreioperationen unterstützen EXIF-Ausrichtung ungleich 1 noch nicht. Einzelne Decoder und Operationen sind nicht sofort abbrechbar. Ein vollständiger manueller Druck-/Zwischenablage-/Drag-and-drop-Durchlauf und sämtliche Referenzdialoge sind nicht nachgewiesen.

Eine große Effektgalerie, 8BF, Audio/Video und KI-Dienste gehören nicht zum aktuellen Kernumfang. Weitere optionale Funktionen und frühere Referenzbeobachtungen stehen unter [Erweiterungsideen](docs/EXTENSION_IDEAS.md) und [Referenz](reference/REFERENCE.md); sie sind keine fertig implementierten Zusagen.

## Entwicklung und Auslieferung

[Architektur](docs/ARCHITECTURE.md) beschreibt Zuständigkeiten, Threads und Speicherverträge; [Entwicklung](docs/DEVELOPMENT.md) die reproduzierbaren Prüfabläufe. `CMakeLists.txt` ist die maßgebliche Versionsquelle. Der geprüfte Wartungsstand trägt 0.5.0; bestehende Profile und Werkzeugpakete bleiben kompatibel.

Die gemeinsamen Ausgaben liegen unter `dist/uxzView`, `dist/uxzView-win64.zip` und `dist/uxzView-source.zip`; Quellhinweise und Prüfsummen liegen ebenfalls direkt unter `dist`. Die Release-Paketierung prüft eigene Binärdateien und Drittanbieter-Quellen und schließt private Profile aus. Werkzeugpakete unter `dist/tools` sind eigenständige optionale Downloads. Kein zusätzliches Release- oder GitHub-Quellverzeichnis. [Release-Anleitung](docs/RELEASING.md), [GitHub-Vorbereitung](docs/GITHUB.md).
