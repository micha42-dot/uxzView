# Einstellungen und Symbole in üxzView

**Optionen → Einstellungen** enthält elf Bereiche in einer vertikalen Kategorienliste. Die Vorschauleiste hat den eigenen Bereich **Vorschauen**. Das Fenster behält beim Kategorienwechsel seine Größe; auf kleinen Bildschirmen kann der jeweilige Inhalt vertikal scrollen. Bedienelemente und Auswahllisten haben größere Klickflächen, die Dropdown-Pfeile bleiben klein. Zahlenfelder verwenden ihre benötigte Breite. „OK“ speichert und übernimmt die Werte im aktuellen Hauptfenster. „Abbrechen“ verwirft die Dialogänderungen. Der ausdrücklich beschriftete Knopf „Caches jetzt leeren“ wirkt sofort; er verändert keine Bilddateien und entfernt das angezeigte Bild nicht.

| Bereich | Verfügbare Einstellungen |
| --- | --- |
| Anzeige | Bildanpassung, Zentrierung, Glättung, ICC-Anzeige nach sRGB, Anzeigeschärfe, Hintergrundfarbe |
| Oberfläche | Farbschema (Systemdesign/Hell/Dunkel) und Sprache (Deutsch/English/한국어/日本語/Français), beide sofort nach OK; Symbol-/Status-/Menüleiste, Aktionssuche, Symbolgröße 20–32 px, optionale Symbolbeschriftungen, Fenstergröße/-position merken |
| Vorschauen | Nachbarleiste ein/aus, unabhängig 1–5 vorherige und kommende Bilder, Hintergrund der Vorschaubilder |
| Navigation | Natürliche/alphabetische/Datums-/Größensortierung, Reihenfolge umkehren, Wiederholung am Ordnerende, Zoom und Scrollposition beim Bildwechsel behalten, optionale Speicherabfrage für ungespeicherte Änderungen |
| Steuerung | Doppelklick-Aktion (Vollbild umschalten oder Vergrößern/Verkleinern um den Mauszeiger), Bildnavigation mit Pfeiltasten (Links/Rechts, Hoch/Runter oder beides); diese Unterauswahl bezieht sich auf die Bildnavigation der Bildansicht (Haupt- und Vollbildfenster), nicht auf das Verhalten von fokussierten Drehfeldern, Listen oder anderen Steuerelementen; beides sofort wirksam, Standard bleibt bisheriges Verhalten |
| Vollbild | Eigene Bildanpassung (100 %, ganzes Bild, nur große Bilder, Breite, Höhe, kurze Seite), Zielbildschirm bei mehreren Monitoren, Mauszeiger ausblenden, eigene Hintergrundfarbe (Standard Schwarz), optionale Informationsanzeige mit vier Ecken und auswählbaren Datei-/Bild-/EXIF-Daten; keine Bildflächenumrandung |
| Diashow | Anzeigedauer, Start im Vollbild, Wiederholung, Zufallsreihenfolge; sonst Sortierung unter Navigation |
| Screenshots | Einstellbare Tastenkombination, Verzögerung, Bildschirm unter Mauszeiger oder Hauptbildschirm; unter Windows optional systemweites Kürzel während üxzView läuft |
| Freistellen | Sechs Standardverhältnisse und bis zu 32 eigene Formate (Breite-/Höhe-Felder mit Hinzufügen/Entfernen) |
| Speichern | Ursprungsformat bevorzugen (Standard) oder festes, tatsächlich schreibbares Standardformat; Portabelmodus nach Neustart, Datenordner anzeigen/öffnen, Benutzerprofil als portable Kopie übernehmen |
| Leistung | Bild-/Miniaturcachebudget einschließlich Abschalten, eingebettete RAW-Vorschauen, anfängliche Zahl paralleler Batchdateien, Caches leeren |

Ohne vorhandene Voreinstellung werden ungespeicherte Bearbeitungen beim Bildwechsel und Schließen ohne Nachfrage verworfen. Bereits ausdrücklich gespeicherte Entscheidungen bleiben erhalten. Unter **Navigation → Beim Bildwechsel und Schließen nach ungespeicherten Änderungen fragen** lässt sich die Abfrage mit Speichern, Verwerfen und Abbrechen ein- oder ausschalten. Die Einstellung gilt auch beim bewussten Neuladen, beim Ersetzen oder Leeren des Dokuments, beim Seitenwechsel in mehrseitigen Bildern und beim Schließen über Escape oder „Beenden“. Bei eingeschalteter Abfrage fragt eine Diashow vor dem ersten Wechsel von einem bearbeiteten Bild; Abbrechen stoppt die Diashow. Abbrechen oder ein fehlgeschlagener Speicherversuch erhält Bearbeitung und Undo/Redo. Escape im Vollbild wechselt weiterhin zunächst zurück ins Fenster; dabei bleiben Bearbeitungen erhalten. Gespeicherte Originaldateien werden durch das Verwerfen nicht verändert. Bei einem fehlgeschlagenen ausdrücklichen Öffnen oder einer erfolglosen Suche nach dem nächsten lesbaren Bild bleibt die bisherige Bearbeitung erhalten.

**Navigation → Unlesbare Dateien** gilt für das Durchblättern von Ordnern:

- **Überspringen** (Standard): Dateien mit unbekannter Endung werden nicht indiziert; beschädigte oder nicht unterstützte Bilddateien werden beim Laden übersprungen. Ein Durchlauf ist begrenzt, auch wenn alle übrigen Dateien defekt sind.
- **Meldung mit Dateiname**: Alle normalen Dateien werden berücksichtigt. Bei einem Fehler erscheinen vollständiger Dateipfad und Ursache; danach kann weitergeblättert werden.
- **Schwarze Anzeige**: Dieselben Dateien werden berücksichtigt; unlesbare Einträge erscheinen ohne Popup als schwarze Fläche. Dateiname und Position bleiben sichtbar, Bearbeiten und Speichern sind gesperrt. Maus und Tastatur blättern weiter.

Die Änderung gilt nach „OK“ unmittelbar. Ein ausdrücklich über „Öffnen“ gewählter unlesbarer Pfad meldet unabhängig von dieser Einstellung einen Fehler mit Dateiname. Fehlversuche werden nicht dauerhaft gesperrt: Eine inzwischen reparierte Datei wird erneut gelesen.

Wählt ein Navigationsbefehl wieder das bereits angezeigte Bild oder dieselbe
Seite, bleiben Bearbeitung und Undo erhalten; eine Speicherabfrage entfällt.
Das gilt auch für einen Ordner mit nur einem Bild und Seitenbefehle am Anfang
oder Ende einer mehrseitigen Datei. Bewusstes Neuladen bleibt möglich.

Die Modi **An Fenster anpassen** und **Nur große Bilder verkleinern** zeigen das gesamte Bild ohne automatische Scrollleisten. Das Mausrad blättert dann durch die Bilddateien. Bei tatsächlichem Zoom scrollt es weiterhin im Bild; Strg+Mausrad verändert den Zoom.

Beim Verlassen des Vollbilds werden manueller Zoom und Verschiebung aufgehoben. Der zuvor gewählte Anzeigemodus des Fensters wird neu berechnet, einschließlich Vorschauleiste und maximiertem Fenster. **Originalgröße** kehrt zu 100 % zurück. Der Vollbildmodus hat eine eigene Anpassung und einen wählbaren Zielmonitor; ein nicht mehr angeschlossener Monitor fällt auf den aktuellen Bildschirm zurück. Die Bildschirmwahl gilt beim nächsten Vollbildeintritt.

Die Vorschaubilderleiste hat unter **Ansicht** einen eigenen Abschnitt vor Vollbild; die Befehlssuche steht direkt unter dem Titelleisten-Schalter. Beide Schalter bleiben erhalten und verwenden dieselben Einstellungen. Die Anzahl wird ausschließlich im Einstellungsdialog geändert. Die Vorschaugruppe wird mittig platziert, auch am Ordneranfang/-ende und bei unterschiedlicher Anzahl links und rechts. In schmalen Fenstern ist sie horizontal scrollbar. Die Leiste und ihre Hintergrundarbeit bleiben optional.

**Ansicht → Informationen anzeigen (Vollbildmodus)** steht direkt unter **Anzeige-Optionen (Fenstermodus)**. Dort lässt sich die Einblendung aktivieren und eine der vier Ecken auswählen. Einzeln wählbar sind Dateiname, Auflösung, DPI, Dateigröße, Format, Zoom, Kamera, Objektiv, Aufnahmedatum, Belichtungszeit, Blende, ISO, Brennweite und Copyright. Fehlende Angaben entfallen. Dieselben Optionen stehen unter **Einstellungen → Vollbild**. Die Anzeige liest bereits geladene Metadaten, verändert keine Bildpixel und nimmt keine Mausklicks weg; sie bleibt im Fenstermodus unsichtbar.

**Ansicht → Anzeige-Optionen (Fenstermodus)** markiert genau einen Anpassungsmodus
mit einem Häkchen. Die zuletzt gewählte Fenster-/Bildschirmvariante bleibt
auch nach einem Neustart erkennbar. Manuelles Zoomen zeigt vorübergehend „Kein
Anpassen durchführen“; die automatische Voreinstellung für kommende Bilder bleibt
erhalten. Im Vollbild zeigt dieses Fenstermenü die Anpassung für die Rückkehr ins
Fenster. Alte Profile ohne genaue Variantenangabe markieren bei „nur große Bilder“
standardmäßig „Nur große Bilder an Bildschirm anpassen“.

Die Fenstereinpassung lässt sich auch im Vollbild über Einstellungen ändern; sie
gilt bei der Rückkehr ins Fenster. Das Schließen eines älteren Fensters setzt eine
inzwischen neu gewählte Einpassung nicht zurück. „OK“ übernimmt die im Dialog
angezeigte Einpassung auch in ein Fenster mit veraltetem lokalem Anzeigestatus.

Bei automatischer Bildanpassung wird die passende Größe für jedes Bild neu berechnet,
auch wenn „Zoomfaktor beim Bildwechsel beibehalten“ aktiv ist. Die Zoomsperre übernimmt
bewusst gewählte manuelle Vergrößerungen; sie ersetzt die Einpassung nicht durch den
Zahlenwert des vorherigen Bildes. So behalten eingepasste Panoramen keine Scrollbereiche.

Anzeigeparameter ändern keine Originalpixel. Bildanpassung und die beiden Navigationssperren werden mit ihren Menüaktionen konsistent gespeichert. Symbolbeschriftungen benötigen zusätzliche Breite; die Toolbar bietet bei Platzmangel ihr übliches Überlaufmenü. Die Schaltflächen für vorherige/nächste Seite erscheinen nur bei mehrseitigen Dateien.

Das Bildcachebudget steht standardmäßig auf 192 MiB, Miniaturen auf 16 MiB. Originale, wiederverwendbare farbkorrigierte/geschärfte Anzeigen und bereits für statische PNG-/BMP-Vorschauen dekodierte Vollbilder teilen sich das Bildcachebudget. Optionale Puffer nutzen nur freien Platz und weichen zuerst für neue Originale; nahe Nachbarn haben Vorrang vor weiter entfernten. Maximal 96 MiB Pixeldaten pro Original werden im Bildcache aufgenommen. Alle vier Cachearten zusammen bleiben innerhalb ihrer angeforderten Budgets und höchstens des Bildspeicherbudgets. Ein kleineres Bildspeicherbudget kann daher die wirksamen Cachegrenzen reduzieren. Neue Batch-Jobs berücksichtigen den aktuell von diesen Caches gehaltenen Speicher. Das Wiederverwenden dekodierter Vorschaubilder entfällt, wenn Bild- oder Miniaturcache abgeschaltet sind.

Aktuelles Bild, Undo, Animationssitzungen, Viewport-Puffer sowie Encoder-/Decoder-Arbeit benötigen weiterhin zusätzlichen Speicher; dies ist kein Prozess-RAM-Limit. Ein höheres Budget garantiert nicht, dass alle Bilder ohne erneutes Dekodieren verfügbar bleiben. Ausgeschaltete RAW-Vorschauen verhindern eingebettete RAW-Miniaturen; der vollständige RAW-Decoder bleibt nutzbar. Die Batchparallelität gilt als Voreinstellung für neue Ketten und kann im Batchdialog weiterhin geändert werden.

Die beiden Glättungsaktionen unter **Ansicht** sind unabhängig: „für Anpassen“
gilt bei automatischer Einpassung, „für Zoomen“ bei manueller Vergrößerung und
100-Prozent-Ansicht. Alte Profile übernehmen ihren bisherigen Wert zunächst für
beide Aktionen. Intern bleiben `view/smooth` und `view/smoothFit` getrennt;
Zoomen liest diese Werte nicht erneut vom Datenträger.

Zahlenfelder und Auswahllisten reagieren auf das Mausrad erst mit Tastaturfokus. Dadurch ändert das Scrollen einer Seite keine Werte versehentlich. Checkboxen sind über ihre gesamte Zeile anklickbar; längere Texte umbrechen bei schmalen Fenstern. Abhängige Vorschauoptionen und das Auslagerungsbudget werden zusammen mit ihren Beschriftungen deaktiviert, wenn die zugehörige Funktion ausgeschaltet ist. Gespeicherte Werte bleiben dabei erhalten.

## Bedeutung der Symbole

Die eigenen SVG-Konturen sind im Quellcode unter `src/ui/icons.cpp` definiert. Farbe und deaktivierter Zustand folgen dem gewählten Farbschema; Rasterungen werden nach Größe, Farbe und Pixeldichte zwischengespeichert. Es werden keine externen Iconpakete oder Netzwerkzugriffe benötigt.

| Symbol | Bedeutung |
| --- | --- |
| Ordner | Bild öffnen |
| Bildschirm mit Play-Dreieck | Diashow |
| Diskette / Drucker / Papierkorb | Speichern unter / Drucken / Systempapierkorb |
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

Die zehn Bereiche erschließen die vorhandenen Funktionen und tatsächlich angebundenen Optionen. Eine vollständige IrfanView-Einstellungsreferenz ist damit nicht umgesetzt. Dateizuordnungen werden über die beiliegenden Windows-Registrierungsskripte beziehungsweise den Linux-/macOS-Desktop verwaltet (siehe FILE_ASSOCIATIONS.md). Unter anderem fehlen Scanner/TWAIN-Konfiguration, vollständige Monitorprofil-/Softproof-Verwaltung und weitere spezialisierte Druck-/Dateioptionen. Eine zusätzliche Effektgalerie gehört nicht zum Kernumfang; solche Erweiterungen sollen optional bleiben. Einstellungen einzelner Bearbeitungsschritte und Batchprofile liegen weiterhin in ihren jeweiligen Dialogen.


## Sprache und Screenshot-Kürzel

Die Oberfläche verwendet eingebettete Übersetzungen. **Optionen → Einstellungen → Oberfläche → Sprache** wechselt nach „OK“ ohne Neustart; „Abbrechen“ behält die bisherige Sprache. Menüs, Werkzeugtipps, Befehlssuche, Schriftwahl und neue Dialoge folgen der Auswahl. Bild, ungespeicherte Bearbeitungen, Auswahl, Zoom und Undo bleiben erhalten, auch im Vollbild. Qt-Standarddialoge nutzen die passenden mitgelieferten qtbase-Sprachdateien. Zahlenfelder folgen der gewählten Oberflächensprache; ein Sprachwechsel sortiert den aktuellen Ordner nicht neu. Bibliotheksdiagnosen und einige technische Fehlermeldungen können weiterhin die Ausgangssprache verwenden. Die Übersetzungen sind funktional geprüft; eine muttersprachliche Sprachprüfung steht aus.

Das Screenshot-Kürzel ist ein einzelner Tastendruck mit Modifikatoren. Ein leeres Feld schaltet es ab. Belegte üxzView-Kürzel werden beim Speichern abgewiesen. Unter Windows ist auf Wunsch ein globales Kürzel verfügbar (F1–F24, Buchstaben, Ziffern oder Druck, mit Modifikatoren). Bei einem Systemkonflikt bleibt das lokale Kürzel verfügbar; der Tooltip des Screenshot-Menüeintrags zeigt den Fehler. Globale Kürzel auf Linux/macOS und Screenshot-Portale für Wayland stehen noch aus.

## Freistellen mit Seitenverhältnis

**Bearbeiten → Freistellen in Aspect-Ratio** bietet 16:9, 9:16, 1:1, 4:3, 3:2 und 21:9 sowie gespeicherte eigene Verhältnisse. Die Wahl setzt einen passenden Rahmen; Linksziehen zeichnet ihn im gewählten Verhältnis neu, Rechtsziehen verschiebt ihn. Ecken und Kanten lassen sich anschließend mit der linken Maustaste nachjustieren. Ein gewähltes Verhältnis bleibt dabei erhalten; der Rahmen bleibt innerhalb des Bildes. Eckgriffe halten die gegenüberliegende Ecke fest, seitliche Griffe die gegenüberliegende Kante und die Mitte der zweiten Achse. Dieselben Griffe stehen bei einer freien Auswahl zur Verfügung. **Freistellen / Strg+Y** ist bei vorhandener Auswahl aktiv, übernimmt den Ausschnitt und lässt sich rückgängig machen. Ein Linksklick zum Aufheben der Auswahl löst auch die Verhältnisbindung. Reine Anzeigeauffrischungen behalten Rahmen und Verhältnis bei. Kleine Ausschnitte werden auf ganze Bildpixel gerundet.

Unter **Optionen → Einstellungen → Freistellen** lassen sich bis zu 32 eigene Verhältnisse angeben, z. B. `5:4`, `2:3` oder `239:100`. „Hinzufügen“ legt eine Zeile mit getrennten Zahlenfeldern für Breite und Höhe an; „Entfernen“ löscht die ausgewählte Zeile. Breite und Höhe sind ganze Zahlen von 1 bis 10000. Doppelte Verhältnisse werden zusammengefasst. Die Formate stehen beim nächsten Öffnen des Untermenüs bereit; Abbrechen verwirft Änderungen an den Einstellungen.

Im Vollbild wechselt ein Linksklick zum vorherigen und ein Rechtsklick zum nächsten Bild, auch auf dem Hintergrund. Ziehen löst keinen Bildwechsel aus; Mitteltaste und Modifikatortasten behalten ihre Bearbeitungsfunktion. Enter oder Escape verlässt das Vollbild.

Details zum vollständigen Portabelmodus, Datenübernahme und Verschieben: [PORTABLE.md](PORTABLE.md).

Unter Größe ändern steht Pixelverdopplung / Pixel-Art direkt im Filter-Dropdown. Die Methode arbeitet ohne Glättung; Nachschärfen, Gamma-Resampling und der schnelle Verkleinerungsfilter sind dafür deaktiviert. Für gleichmäßige Blöcke 200 %, 300 % oder andere ganze Vielfache einstellen; Doppelt setzt die Größe auf das Zweifache. Halb bzw. 50 % verkleinert ohne Glättung, indem Quellpixel ausgewählt werden; feine Details können dabei verloren gehen. Transparenz und 16-Bit-Pixelwerte bleiben erhalten.

Größenvorgaben, Halb/Doppelt und Seiten vertauschen aktivieren die absoluten Größenfelder. Neue eigene Vorgaben werden erst mit OK gespeichert; Abbrechen verwirft sie. Automatisch angepasste DPI sind berechnet und daher nicht editierbar. In diesem Modus werden Pixel statt cm/inch eingegeben; nach dem Ausschalten sind die manuellen DPI wieder verfügbar. Dezimalzahlen folgen der Oberflächensprache, beispielsweise 2,5 in der deutschen Oberfläche. Eingaben werden beim Verlassen des Feldes bzw. mit Enter übernommen.

Manuelle und automatisch berechnete DPI liegen zwischen 1 und 100000. Extremes Skalieren kann diese Grenze erreichen; die Druckgröße wird dann nicht mehr exakt erhalten. Ungültige Auflösungswerte werden vor der Verarbeitung abgelehnt.

Die Einstellungsseiten enthalten die Bedienelemente ohne begleitende Anleitungsabsätze. Unter Speichern bleibt der aktuell aktive Modus sichtbar; das schreibgeschützte Datenordnerfeld lässt sich markieren und kopieren. Der Portabel-Schalter wird weiterhin erst nach erfolgreicher Übernahme und Neustart wirksam.

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
standardmäßig übersprungen; freigegebene Ersetzungen werden atomar geschrieben und sind danach nicht mehr
über eine Dateihistorie rückgängig machbar. „Vorhandene Dateien ersetzen“ legt
keine private Sicherung an; der atomare Einzeldateiersatz ist kein Backup.

Das Ergebnis nennt erfolgreiche und fehlgeschlagene Dateien sowie tatsächliche
Größen vor/nach der Konvertierung und die prozentuale Änderung. Die Größensumme
enthält nur erfolgreiche Ausgaben. Optional liegt ein UTF-8-CSV-Protokoll mit
Quelle, Ziel, Ergebnis und Bytegrößen im Zielordner; jeder Lauf bekommt eine eigene
Datei. Ein Protokollfehler verändert bereits erfolgreich erzeugte Bilder nicht.
Die Größenstatistik bezieht sich auf die Bilddateien; CSV-Dateien sind darin nicht enthalten. Eine Vorabschätzung wird nicht berechnet.

## Navigation und Löschen

Ordnernavigation und Diashow berücksichtigen die Dateiendungen der aktivierten Bildformate. Videos, Textdokumente und andere nicht unterstützte Dateien werden übersprungen. Die Ordnerauflistung öffnet nicht jede Datei zur Erkennung; beschädigte Dateien mit einer unterstützten Bildendung bleiben deshalb sichtbare Decoderfehler. Beim ausdrücklichen Öffnen einer nicht lesbaren Datei wird ebenfalls ein Fehler gemeldet.

Löschen verschiebt Bilder in den Papierkorb des Betriebssystems. Wiederherstellen erfolgt dort. Ist der Systempapierkorb nicht verfügbar, wird die Datei nicht ersatzweise endgültig gelöscht. Dateijournal und Dateioperationen-Undo entfallen ab 0.5.0. Das Rückgängig für Bildbearbeitungen bleibt erhalten.

Die untere Infoleiste behält Bild- und Auswahlinformationen auch beim Überfahren von Menü- und Toolbar-Aktionen. Befehlsbeschreibungen stehen als Tooltips am jeweiligen Bedienelement.

Under **Performance → Image buffer budget (restart)**, zero selects automatic
half-RAM sizing (512 MiB to 8 GiB); a positive MiB value is an override. The
preference is performance/memoryLimitMiB. It is separate from caches/history and
applies after restarting. The resize dialog reports its maximum dimensions for
the selected filter and aspect ratio. See [large images](LARGE_IMAGES.md).

Performance also exposes disk backing for large original-pixel buffers:
performance/swapEnabled defaults to true; performance/swapLimitMiB defaults to
65536 MiB, aggregated across active mappings in this process. Changes affect new
allocations immediately; existing images remain usable. See the storage and
format limits in [large images](LARGE_IMAGES.md).

## Kompressionsoptionen beim Speichern

**Datei → Speichern unter** öffnet den gewohnten nativen Dateidialog; daneben
erscheint ein kompaktes Begleitfenster mit den Kompressionseinstellungen des
gewählten Formats (z. B. „JPEG-Optionen speichern"). Beide Fenster bleiben vor
dem Speichern bedienbar; das Begleitfenster folgt dem Dateidialog und weicht bei
Platzmangel auf die linke Seite aus. Das Schließen des Begleitfensters bricht
den Speichervorgang nicht ab — gespeichert wird mit den zuletzt gewählten
Einstellungen. Die Dateiformatliste des nativen Dialogs wird kanonisch
gematcht, sodass z. B. ein als JFIF geführter JPEG-Eintrag weiterhin das
JPEG-Format wählt.

- **Qualität (0–100, Slider + Zahl):** JPEG (jpegli-Encoder), WebP und AVIF/HEIF;
  PNG nutzt stattdessen eine verlustfreie Stufe 0–9.
- **Zieldateigröße (KB/MB):** ermittelt per begrenzter Suche über den echten
  Encoder die höchste passende Qualität; bei aktivierter Option ist der manuelle
  Regler gesperrt und zeigt das Ergebnis. Unerreichbare Ziele melden einen
  Fehler; die Auflösung wird nie verändert.
- **Formatoptionen:** JPEG progressiv und Chroma-Subsampling; WebP/AVIF verlustfrei
  und Aufwand; GIF interlaced. Nur unterstützte Schalter werden angezeigt.
- **Metadaten:** EXIF/IPTC/XMP und ICC-Profil einzeln abwählbar; die
  EXIF-Ausrichtung wird auf die Pixel angewendet.
- **Transparenz:** Für Formate ohne Alphakanal wählbar — auf Weiß reduzieren
  (bisheriges Verhalten), Originalpixel behalten oder bei jedem Speichern
  fragen. Die Warnung vor Formatverlusten erscheint nur noch bei tatsächlich
  vorhandener Transparenz statt bei bloßem Alphakanal und lässt sich zusätzlich
  über `save/warnLosses` abschalten.
- **Profile:** Kombinationen aus Format und Einstellungen lassen sich benennen,
  laden, überschreiben (mit Nachfrage) und löschen; die letzten Einstellungen
  werden beim nächsten Speichern wiederhergestellt.
- **Stapelverarbeitung:** Qualität und Zieldateigröße stehen im
  Batch-Dialog; die Zielgröße wird pro Bild einzeln optimiert.

Die Warnung vor Formatverlusten lässt sich zusätzlich über `save/warnLosses`
abschalten; sie erscheint ohnehin nur noch bei tatsächlich vorhandener
Transparenz statt bei bloßem Alphakanal.

## Standardformat beim Speichern

Unter **Speichern** bleibt **Ursprüngliches Dateiformat bevorzugen** der Standard.
**Festgelegtes Dateiformat bevorzugen** aktiviert die Liste der verfügbaren Encoder.
Die Liste wird erst beim Öffnen dieser Seite ermittelt. Die Bedienelemente bleiben
gut anklickbar; das Dialogfenster startet kompakter mit 760 × 460 logischen Pixeln.

Die Schlüssel `save/preferOriginal` (Standard `true`) und `save/format` (Standard
`png`) werden mit dem Profil gespeichert und beim Portabel-Import übernommen.
Nicht schreibbare Quellen und nicht mehr verfügbare Wunschformate führen zu
„Speichern unter“ mit einer tatsächlich schreibbaren Vorauswahl. Der gewählte
Dateityp und eine ausdrücklich eingegebene unterstützte Endung können das Format
für diesen einen Export ändern; die globale Präferenz bleibt erhalten. Eine
fehlende Endung ergänzt der Dialog passend zum Dateityp. Vorhandene Ziele werden
weiterhin ausdrücklich bestätigt und atomar ersetzt.

Weicht das bevorzugte Format vom Original ab, wird ein neuer Dateiname mit der
passenden Endung vorgeschlagen. Unveränderte JPEG-Dateien werden durch normales
„Speichern“ nicht neu komprimiert; „Speichern unter“ bleibt ein bewusster Export.
Der Standard-JPEG-Encoder ist jpegli. Qualität und Verlusthinweise bleiben gültig;
Transparenz wird auf Weiß reduziert. JPEG XL bleibt ein eigenes Format.
