# Tastatur und Maus

Die Bedienung folgt den erfassten 4.76-Menüs sowie den [offiziellen Tasten- und Mausreferenzen](https://www.irfanview.net/help/guida/hlp_hotkeys.htm), geprüft am 18.10.2026. Die Online-Hilfe ist eine ergänzende Quelle; ihre Versionsbindung ist nicht für jede Taste gesondert nachgewiesen. Ein nativer Vergleich des vollständigen Ablaufs steht aus.

`resources/menus.de.json` enthält die Menü-Kürzel. `resources/shortcuts.de.json` hält zusätzliche Aliase, Kontexttasten und Mausaktionen mit ihrer Quelle fest. Unter F1 zeigt üxzView die tatsächlich verbundenen Menütasten und diese ergänzenden Aktionen.

## Rahmen und Lupe

1. Linke Maustaste im Bild drücken, ein Rechteck aufziehen und loslassen.
2. Über der Markierung erscheint ein selbst gezeichneter Lupenzeiger.
3. Ein Linksklick innerhalb der Markierung vergrößert diesen Bereich und richtet ihn im Fenster aus. Die Bildpixel werden nicht bearbeitet.

Strg+Linksklick innerhalb der Markierung hebt sie auf. Rechtsziehen innerhalb verschiebt die Markierung; Strg beschränkt die Bewegung auf die Y-Achse, Shift auf X. Rechte oder mittlere Maustaste außerhalb verschiebt ein größeres Bild. Klicks ohne Ziehen erzeugen keine winzige Markierung.

Im Vollbild blättert ein **Linksklick zurück**, ein **Rechtsklick weiter**, auch auf dem Hintergrund. Ziehbewegungen markieren oder verschieben weiterhin und blättern nicht. Enter oder Escape verlässt das Vollbild; ein Doppelklick im Fenster öffnet es. Bei einem festen Freistellverhältnis zeichnet Linksziehen einen neuen Rahmen statt die Lupe auszulösen.

## Tasten nach Kontext

| Kontext | Taste | Verhalten |
| --- | --- | --- |
| Keine Markierung, kein horizontaler Scrollbereich | Links / Rechts | Vorheriges / nächstes Bild |
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
| Hauptfenster | Enter / Escape | Vollbild umschalten / Vollbild verlassen |
| Hauptfenster | F1 / A | Tastaturhilfe / Über üxzView |
| Sichtbare Toolbar | Shift+T | Zoomfeld auswählen |
| Zoomfeld | Enter / Escape | Eingabe übernehmen / verwerfen und zur Bildansicht zurückkehren |
| Geöffnete Datei | Shift+P / Strg+N | Dateipfad kopieren / Datei in weiterem Fenster öffnen |

F6 ergänzt F2 (Umbenennen), U ergänzt F5 (Aktualisieren), Strg+Einfg ergänzt Strg+C und Shift+Einfg ergänzt Strg+V. Die sonstigen Menü-Kürzel, etwa Strg+R, L/R, H/V, Strg+Z/Strg+J und Strg+Y, bleiben erhalten. Standard-Textbearbeitung in Eingabefeldern hat ihren eigenen Kontext.

## Noch offen

Nicht alle historischen oder pluginbezogenen Hotkeys sind implementiert. Dazu gehören unter anderem das separate Strg+Shift-Maus-Lupenfenster, Strg+Shift-Pfeil-Feinrotation, sämtliche proportionalen Maus-/Randgriffe, Tastenneubelegung sowie vollständige Vollbild-/Slideshow-Spezialbelegungen. Die Hilfe zeigt diese nicht als verfügbare Funktionen an. Eine vollständige Prüfung von Tastaturlayouts, Nummernblock, Apple-Command-Zuordnung und nativen Desktop-Fokuswechseln steht aus.
