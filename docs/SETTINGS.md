# Einstellungen und Symbole in üxzView

**Optionen → Einstellungen** enthält zehn Bereiche in einer vertikalen Kategorienliste. „OK“ speichert und übernimmt die Werte im aktuellen Hauptfenster. „Abbrechen“ verwirft die Dialogänderungen. Der ausdrücklich beschriftete Knopf „Caches jetzt leeren“ wirkt sofort; er verändert keine Bilddateien und entfernt das angezeigte Bild nicht.

| Bereich | Verfügbare Einstellungen |
| --- | --- |
| Anzeige | Bildanpassung, Zentrierung, Glättung, ICC-Anzeige nach sRGB, Anzeigeschärfe, Hintergrundfarbe |
| Oberfläche | Farbschema (Systemdesign/Hell/Dunkel) und Sprache (Deutsch/English/한국어/日本語/Français), beide sofort nach OK; Symbol-/Status-/Menüleiste, Aktionssuche, Symbolgröße 20–32 px, optionale Symbolbeschriftungen, Fenstergröße/-position merken |
| Vorschauen | Nachbarleiste ein/aus, unabhängig 1–5 vorherige und 1–5 kommende Bilder, Hintergrund wie Oberfläche / wie Bildhintergrund / transparent |
| Navigation | Natürliche/alphabetische/Datums-/Größensortierung, Reihenfolge umkehren, Wiederholung am Ordnerende, Zoom und Scrollposition beim Bildwechsel behalten, optionale Speicherabfrage (standardmäßig aus) |
| Vollbild | Eigene Bildanpassung (100 %, ganzes Bild, nur große Bilder, Breite, Höhe, kurze Seite), Zielbildschirm bei mehreren Monitoren, Mauszeiger ausblenden, eigene Hintergrundfarbe (Standard Schwarz), optionale Informationsanzeige mit vier Ecken und auswählbaren Datei-/Bild-/EXIF-Daten; keine Bildflächenumrandung |
| Diashow | Anzeigedauer, Start im Vollbild, Wiederholung, Zufallsreihenfolge; sonst Sortierung unter Navigation |
| Screenshots | Einstellbare Tastenkombination, Verzögerung, Bildschirm unter Mauszeiger oder Hauptbildschirm; unter Windows optional systemweites Kürzel während üxzView läuft |
| Freistellen | Sechs Standardverhältnisse und bis zu 32 eigene Formate (Breite-/Höhe-Felder mit Hinzufügen/Entfernen) |
| Speicherorte | Portabelmodus nach Neustart, Datenordner anzeigen/öffnen, vorhandenes Benutzerprofil auf Wunsch als portable Kopie übernehmen |
| Leistung | Bild-/Miniaturcachebudget einschließlich Abschalten, eingebettete RAW-Vorschauen, anfängliche Zahl paralleler Batchdateien, Caches leeren |

Beim Bildwechsel werden ungespeicherte Bearbeitungen standardmäßig verworfen. Unter **Navigation → Beim Bildwechsel nachfragen, ob Änderungen gespeichert werden sollen** lässt sich die Abfrage mit Speichern, Verwerfen und Abbrechen einschalten. Die Einstellung gilt auch beim Seitenwechsel in mehrseitigen Bildern. Gespeicherte Originaldateien werden durch das Weiterblättern nicht verändert. Schlägt das Öffnen des nächsten Bildes fehl, bleibt die bisherige Bearbeitung erhalten. Beim Schließen des Fensters wird weiterhin nach ungespeicherten Änderungen gefragt.

Die Modi **An Fenster anpassen** und **Nur große Bilder verkleinern** zeigen das gesamte Bild ohne automatische Scrollleisten. Das Mausrad blättert dann durch die Bilddateien. Bei tatsächlichem Zoom scrollt es weiterhin im Bild; Strg+Mausrad verändert den Zoom.

Beim Verlassen des Vollbilds werden manueller Zoom und Verschiebung aufgehoben. Der zuvor gewählte Anzeigemodus des Fensters wird neu berechnet, einschließlich Vorschauleiste und maximiertem Fenster. **Originalgröße** kehrt zu 100 % zurück. Der Vollbildmodus hat eine eigene Anpassung und einen wählbaren Zielmonitor; ein nicht mehr angeschlossener Monitor fällt auf den aktuellen Bildschirm zurück. Die Bildschirmwahl gilt beim nächsten Vollbildeintritt.

Die Vorschaubilderleiste hat unter **Ansicht** einen eigenen Abschnitt vor Vollbild; die Befehlssuche steht direkt unter dem Titelleisten-Schalter. Beide Schalter bleiben erhalten und verwenden dieselben Einstellungen. Die Anzahl wird ausschließlich im Einstellungsdialog geändert. Die Vorschaugruppe wird mittig platziert, auch am Ordneranfang/-ende und bei unterschiedlicher Anzahl links und rechts. In schmalen Fenstern ist sie horizontal scrollbar. Die Leiste und ihre Hintergrundarbeit bleiben optional.

**Ansicht → Informationen anzeigen (Vollbildmodus)** steht direkt unter **Anzeige-Optionen (Fenstermodus)**. Dort lässt sich die Einblendung aktivieren und eine der vier Ecken auswählen. Einzeln wählbar sind Dateiname, Auflösung, DPI, Dateigröße, Format, Zoom, Kamera, Objektiv, Aufnahmedatum, Belichtungszeit, Blende, ISO, Brennweite und Copyright. Fehlende Angaben entfallen. Dieselben Optionen stehen unter **Einstellungen → Vollbild**. Die Anzeige liest bereits geladene Metadaten, verändert keine Bildpixel und nimmt keine Mausklicks weg; sie bleibt im Fenstermodus unsichtbar.

Anzeigeparameter ändern keine Originalpixel. Bildanpassung und die beiden Navigationssperren werden mit ihren Menüaktionen konsistent gespeichert. Symbolbeschriftungen benötigen zusätzliche Breite; die Toolbar bietet bei Platzmangel ihr übliches Überlaufmenü. Die Schaltflächen für vorherige/nächste Seite erscheinen nur bei mehrseitigen Dateien.

Das Bildcachebudget steht standardmäßig auf 192 MiB, Miniaturen auf 16 MiB. Maximal 96 MiB pro Bild werden zusätzlich im Bildcache gehalten. Der aktuelle Bildinhalt, Undo, Encoder und Decoder benötigen unabhängig davon Speicher. Ein höheres Budget ist keine Zusage, dass alle Bilder ohne erneutes Dekodieren verfügbar bleiben. Ausgeschaltete RAW-Vorschauen verhindern eingebettete RAW-Miniaturen; der vollständige RAW-Decoder bleibt nutzbar. Die Batchparallelität gilt als Voreinstellung für neue Ketten und kann im Batchdialog weiterhin geändert werden.

## Bedeutung der Symbole

Die eigenen SVG-Konturen sind im Quellcode unter `src/ui/icons.cpp` definiert. Farbe und deaktivierter Zustand folgen dem gewählten Farbschema; Rasterungen werden nach Größe, Farbe und Pixeldichte zwischengespeichert. Es werden keine externen Iconpakete oder Netzwerkzugriffe benötigt.

| Symbol | Bedeutung |
| --- | --- |
| Ordner | Bild öffnen |
| Bildschirm mit Play-Dreieck | Diashow |
| Diskette / Drucker / Papierkorb | Speichern unter / Drucken / interner Papierkorb |
| Schere / zwei Blätter / Klemmbrett | Ausschneiden / Kopieren / Einfügen |
| Gebogener Linkspfeil | Bildbearbeitung rückgängig |
| Bild mit Infozeichen | Bildinformationen und Metadaten |
| Lupe mit Plus / Minus | Vergrößern / Verkleinern |
| Bildrahmen mit Links-/Rechtszeichen | Vorherige / nächste Bilddatei |
| Blatt mit Links-/Rechtspfeil | Vorherige / nächste Seite innerhalb einer mehrseitigen Datei |
| Zahnrad | Einstellungen |
| Eingekreistes Infozeichen | Über üxzView |

Tooltips benennen die jeweilige Aktion und vorhandene Tastenkürzel. Unter Oberfläche können sichtbare Beschriftungen ergänzt werden. Die Icons wurden in einer hellen und dunklen Testpalette sowie in der Toolbar visuell geprüft; die tatsächliche Erkennbarkeit für jeden Benutzer lässt sich daraus nicht garantieren.

## Noch nicht enthalten

Der bisherige kleine Einstellungsdialog war unvollständig. Die zehn Bereiche erschließen nun die vorhandenen Funktionen und ergänzen tatsächlich angebundene Optionen. Eine vollständige IrfanView-Einstellungsreferenz ist damit nicht umgesetzt. Dateizuordnungen werden über die beiliegenden Windows-Registrierungsskripte beziehungsweise den Linux-/macOS-Desktop verwaltet (siehe FILE_ASSOCIATIONS.md). Unter anderem fehlen Scanner/TWAIN-Konfiguration, vollständige Monitorprofil-/Softproof-Verwaltung und weitere spezialisierte Druck-/Dateioptionen. Eine zusätzliche Effektgalerie gehört nicht zum Kernumfang; solche Erweiterungen sollen optional bleiben. Einstellungen einzelner Bearbeitungsschritte und Batchprofile liegen weiterhin in ihren jeweiligen Dialogen.


## Sprache und Screenshot-Kürzel

Die Oberfläche verwendet eingebettete Übersetzungen. **Optionen → Einstellungen → Oberfläche → Sprache** wechselt nach „OK“ ohne Neustart; „Abbrechen“ behält die bisherige Sprache. Menüs, Werkzeugtipps, Befehlssuche, Schriftwahl und neue Dialoge folgen der Auswahl. Bild, ungespeicherte Bearbeitungen, Auswahl, Zoom und Undo bleiben erhalten, auch im Vollbild. Qt-Standarddialoge nutzen die passenden mitgelieferten qtbase-Sprachdateien. Zahlenfelder folgen der gewählten Oberflächensprache; ein Sprachwechsel sortiert den aktuellen Ordner nicht neu. Bibliotheksdiagnosen und einige technische Fehlermeldungen können weiterhin die Ausgangssprache verwenden. Die Übersetzungen sind funktional geprüft; eine muttersprachliche Sprachprüfung steht aus.

Das Screenshot-Kürzel ist ein einzelner Tastendruck mit Modifikatoren. Ein leeres Feld schaltet es ab. Belegte üxzView-Kürzel werden beim Speichern abgewiesen. Unter Windows ist auf Wunsch ein globales Kürzel verfügbar (F1–F24, Buchstaben, Ziffern oder Druck, mit Modifikatoren). Bei einem Systemkonflikt bleibt das lokale Kürzel verfügbar; der Tooltip des Screenshot-Menüeintrags zeigt den Fehler. Globale Kürzel auf Linux/macOS und Screenshot-Portale für Wayland stehen noch aus.

## Freistellen mit Seitenverhältnis

**Bearbeiten → Freistellen in Aspect-Ratio** bietet 16:9, 9:16, 1:1, 4:3, 3:2 und 21:9. Die Wahl setzt einen passenden Rahmen; Linksziehen zeichnet ihn im gewählten Verhältnis neu, Rechtsziehen verschiebt ihn. **Freistellen / Strg+Y** übernimmt den Ausschnitt und lässt sich rückgängig machen. **Freie Auswahl** hebt die Verhältnisbindung auf. Kleine Ausschnitte werden auf ganze Bildpixel gerundet.

Unter **Optionen → Einstellungen → Freistellen** lassen sich bis zu 32 eigene Verhältnisse angeben, z. B. `5:4`, `2:3` oder `239:100`. „Hinzufügen“ legt eine Zeile mit getrennten Zahlenfeldern für Breite und Höhe an; „Entfernen“ löscht die ausgewählte Zeile. Breite und Höhe sind ganze Zahlen von 1 bis 10000. Doppelte Verhältnisse werden zusammengefasst. Die Formate stehen beim nächsten Öffnen des Untermenüs bereit; Abbrechen verwirft Änderungen an den Einstellungen.

Im Vollbild wechselt ein Linksklick zum vorherigen und ein Rechtsklick zum nächsten Bild, auch auf dem Hintergrund. Ziehen löst keinen Bildwechsel aus; Mitteltaste und Modifikatortasten behalten ihre Bearbeitungsfunktion. Enter oder Escape verlässt das Vollbild.

Details zum vollständigen Portabelmodus, Datenübernahme und Verschieben: [PORTABLE.md](PORTABLE.md).

Unter Größe ändern steht Pixelverdopplung / Pixel-Art direkt im Filter-Dropdown. Die Methode arbeitet ohne Glättung; Nachschärfen, Gamma-Resampling und der schnelle Verkleinerungsfilter sind dafür deaktiviert. Für gleichmäßige Blöcke 200 %, 300 % oder andere ganze Vielfache einstellen; Doppelt setzt die Größe auf das Zweifache. Halb bzw. 50 % verkleinert ohne Glättung, indem Quellpixel ausgewählt werden; feine Details können dabei verloren gehen. Transparenz und 16-Bit-Pixelwerte bleiben erhalten.

Größenvorgaben, Halb/Doppelt und Seiten vertauschen aktivieren die absoluten Größenfelder. Neue eigene Vorgaben werden erst mit OK gespeichert; Abbrechen verwirft sie. Automatisch angepasste DPI sind berechnet und daher nicht editierbar. In diesem Modus werden Pixel statt cm/inch eingegeben; nach dem Ausschalten sind die manuellen DPI wieder verfügbar. Dezimalzahlen folgen der Oberflächensprache, beispielsweise 2,5 in der deutschen Oberfläche. Eingaben werden beim Verlassen des Feldes bzw. mit Enter übernommen.

Manuelle und automatisch berechnete DPI liegen zwischen 1 und 100000. Extremes Skalieren kann diese Grenze erreichen; die Druckgröße wird dann nicht mehr exakt erhalten. Ungültige Auflösungswerte werden vor der Verarbeitung abgelehnt.

Die Einstellungsseiten enthalten die Bedienelemente ohne begleitende Anleitungsabsätze. Unter Speicherorte bleibt der aktuell aktive Modus sichtbar; das schreibgeschützte Datenordnerfeld lässt sich markieren und kopieren. Der Portabel-Schalter wird weiterhin erst nach erfolgreicher Übernahme und Neustart wirksam.

## Stapelverarbeitung

Der Dialog hat die Reiter **Verarbeitung** und **Ergebnis**, ohne äußere Scrollleiste.
Dateilisten und Ergebnisse können bei vielen Einträgen weiterhin selbst scrollen.
Die Kette beginnt leer. Schritte hinzufügen oder ein Profil laden; genau ein
Speicherschritt steht am Ende. Suche und Formatfilter bestimmen die verarbeiteten
Dateien, deren Anzahl über der Dateiliste angezeigt wird. Beim Hinzufügen lassen
sich ebenfalls einzelne Formate auswählen; die Detailansicht unterstützt die
übliche Sortierung nach Dateigröße. „Starten“ prüft die Ziele automatisch und
verarbeitet die Dateien im Hintergrund. „Abbrechen“ stoppt zwischen Arbeitsschritten;
ein bereits laufender Decoder muss zuerst zurückkehren. Bestehende Ziele werden
standardmäßig übersprungen; freigegebene Ersetzungen bleiben gesichert und über
das Dateijournal rückgängig machbar.

Das Ergebnis nennt erfolgreiche und fehlgeschlagene Dateien sowie tatsächliche
Größen vor/nach der Konvertierung und die prozentuale Änderung. Die Größensumme
enthält nur erfolgreiche Ausgaben. Optional liegt ein UTF-8-CSV-Protokoll mit
Quelle, Ziel, Ergebnis und Bytegrößen im Zielordner; jeder Lauf bekommt eine eigene
Datei. Ein Protokollfehler verändert bereits erfolgreich erzeugte Bilder nicht.
Die Größenstatistik bezieht sich auf die Bilddateien; Journal-Sicherungen und
CSV-Dateien sind darin nicht enthalten. Eine Vorabschätzung wird nicht berechnet.
