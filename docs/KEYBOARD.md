# Tastatur und Maus

Die Bedienung folgt den erfassten 4.76-Menüs sowie den [offiziellen Tasten- und Mausreferenzen](https://www.irfanview.net/help/guida/hlp_hotkeys.htm), geprüft am 18.10.2026. Die Online-Hilfe ist eine ergänzende Quelle; ihre Versionsbindung ist nicht für jede Taste gesondert nachgewiesen. Ein nativer Vergleich des vollständigen Ablaufs steht aus.

`resources/menus.de.json` enthält die Menü-Kürzel. `resources/shortcuts.de.json` hält zusätzliche Aliase, Kontexttasten und Mausaktionen mit ihrer Quelle fest. Unter F1 zeigt üxzView die tatsächlich verbundenen Menütasten und diese ergänzenden Aktionen.

## Rahmen und Lupe

1. Linke Maustaste in der Bildfläche drücken, ein Rechteck aufziehen und loslassen.
2. Über der Markierung erscheint ein selbst gezeichneter Lupenzeiger.
3. Ein Linksklick innerhalb der Markierung vergrößert diesen Bereich und richtet ihn im Fenster aus. Die Bildpixel werden nicht bearbeitet.

Strg+Linksklick innerhalb der Markierung hebt sie auf. Rechtsziehen innerhalb verschiebt die Markierung; Strg beschränkt die Bewegung auf die Y-Achse, Shift auf X. Rechte oder mittlere Maustaste außerhalb verschiebt ein größeres Bild. Klicks ohne Ziehen erzeugen keine winzige Markierung.

Ein Auswahlrahmen kann auch auf dem Hintergrund außerhalb des Bildes beginnen.
Sobald er ins Bild reicht, liegt sein Anfang am Bildrand; außerhalb liegende
Anteile gehören nicht zur Auswahl. Das gilt auch für feste Seitenverhältnisse.
Ein Ziehen vollständig im Hintergrund erzeugt keinen Rahmen.

Im Vollbild blättert ein **Linksklick zurück**, ein **Rechtsklick weiter**, auch auf dem Hintergrund. Ziehbewegungen markieren oder verschieben weiterhin und blättern nicht. Enter oder Escape verlässt das Vollbild; ein Doppelklick im Fenster öffnet es (Standard unter **Einstellungen → Steuerung**). Bei einem festen Freistellverhältnis zeichnet Linksziehen einen neuen Rahmen statt die Lupe auszulösen.

## Steuerung (Einstellungen)

Der Bereich **Einstellungen → Steuerung** fasst Maus- und Tastaturverhalten zusammen:

- **Maus → Doppelklick-Aktion:** Standard **Vollbild umschalten** (bisheriges Verhalten). Mit **Vergrößern/Verkleinern** zoomt ein Doppelklick auf dem Bild um den Faktor 2 um die Mausposition (max. 6400 %); ein weiterer Doppelklick stellt Zoom und Scrollposition des vorherigen Zustands wieder her. Das gilt im Fenster und im Vollbild; Klicks außerhalb des Bildes, mit gedrückten Zusatztasten oder bei aktiver Farbpipette behalten ihr bisheriges Verhalten. Besteht eine Auswahl, löst der erste Klick wie bisher die Lupe aus, bevor der Doppelklick umschaltet.
- **Tastatur → Bildnavigation mit Pfeiltasten:** Standard **Links/Rechts** (bisheriges Verhalten), wahlweise **Hoch/Runter** oder **Links/Rechts und Hoch/Runter**. Scrollen bei Zoom, Markierungsbearbeitung mit Shift/Strg/Alt und die Weitergabe an Eingabefelder/Listen bleiben erhalten: Eine deaktivierte Achse wird nicht verbraucht, sodass etwa Hoch/Runter in Listen weiterhin die Liste steuert.

## Tasten nach Kontext

| Kontext | Taste | Verhalten |
| --- | --- | --- |
| Keine Markierung, kein horizontaler Scrollbereich | Links / Rechts | Vorheriges / nächstes Bild (Standard; **Steuerung → Bildnavigation mit Pfeiltasten**) |
| Keine Markierung, kein vertikaler Scrollbereich | Hoch / Runter | Vorheriges / nächstes Bild, wenn unter **Steuerung** aktiviert |
| Vergrößertes Bild | Pfeiltasten | In der jeweiligen Achse scrollen |
| Markierung, keine Scrollleisten | Pfeiltasten | Markierung verschieben |
| Markierung | Shift+Pfeiltasten | Markierung auch bei sichtbaren Scrollleisten verschieben |
| Markierung | Strg+Pfeiltasten | Proportional vergrößern / verkleinern |
| Markierung | Alt+Pfeiltasten | Breite / Höhe ändern |
| Bildansicht | Leertaste / Rücktaste | Nächstes / vorheriges Bild |
| Bildansicht | Bild auf / Bild ab | Vorheriges / nächstes Bild; bei vertikalem Scrollbereich eine Bildschirmhöhe scrollen |
| Mehrseitiges Bild | Strg+Bild auf / Bild ab | Vorherige / nächste Seite |
| Mehrseitiges Bild | Shift+Pos1 / Ende | Erste / letzte Seite |
| Bildansicht | Strg+Pos1 / Ende | Erstes / letztes Bild |
| Bildansicht ohne passenden Scrollbereich | Strg+Links / Rechts, Strg+Leertaste / Rücktaste | Fünf Bilder weiter / zurück |
| Bildansicht | Strg+Ziffer / Strg+Shift+Ziffer | Um Ziffer Bilder weiter / zurück; 0 = 10 |
| Bildansicht | + oder = / - | Vergrößern / verkleinern |
| Bildansicht | Strg+H | Originalgröße |
| Bildansicht | Strg+A | Ganzes Bild markieren / Markierung aufheben |
| Hauptfenster | Enter | Vollbild umschalten |
| Vollbild | Escape | Vollbild verlassen |
| Fenstermodus | Escape | Diashow beenden, sonst Fenster schließen |
| Hauptfenster | F1 / A | Tastaturhilfe / Über üxzView |
| Sichtbare Toolbar | Shift+T | Zoomfeld auswählen |
| Zoomfeld | Enter / Escape | Eingabe übernehmen / verwerfen und zur Bildansicht zurückkehren |
| Geöffnete Datei | Shift+P / Strg+N | Dateipfad kopieren / Datei in weiterem Fenster öffnen |

F6 ergänzt F2 (Umbenennen), U ergänzt F5 (Aktualisieren), Strg+Einfg ergänzt Strg+C und Shift+Einfg ergänzt Strg+V. Die sonstigen Menü-Kürzel, etwa Strg+R, L/R, H/V, Strg+Z/Strg+J und Strg+Y, bleiben erhalten. Standard-Textbearbeitung in Eingabefeldern hat ihren eigenen Kontext.

Beim Bildwechsel und Schließen werden ungespeicherte Bearbeitungen standardmäßig
verworfen. Die gemeinsame Speicherabfrage lässt sich unter
[Einstellungen → Navigation](SETTINGS.md) einschalten. Das reine Verlassen des
Vollbilds verändert Bearbeitungen nicht.

Auswahlrahmen lassen sich an allen Ecken und Kanten mit der linken Maustaste
nachjustieren. Ein gewähltes Seitenverhältnis bleibt erhalten; die rechte
Maustaste verschiebt weiterhin die Auswahl. Im freien Modus bleibt der Klick
ins Innere für die Lupe erhalten. Die Griffe haben bei Vollbildklicks Vorrang vor
der Bildnavigation.

## Noch offen

Nicht alle historischen oder pluginbezogenen Hotkeys sind implementiert. Dazu gehören unter anderem das separate Strg+Shift-Maus-Lupenfenster, Strg+Shift-Pfeil-Feinrotation, Tastenneubelegung sowie vollständige Vollbild-/Slideshow-Spezialbelegungen. Die Hilfe zeigt diese nicht als verfügbare Funktionen an. Eine vollständige Prüfung von Tastaturlayouts, Nummernblock, Apple-Command-Zuordnung und nativen Desktop-Fokuswechseln steht aus.
