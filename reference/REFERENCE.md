# Referenzanalyse

Erfasst am 17.10.2026 mit Windows Computer Use, Accessibility und visueller Untersuchung der installierten Referenz `C:/Program Files/IrfanView/i_view64.exe`. Die Dateiversion wurde separat als `4.76.0.0` bestätigt. In der Anwendung wurde `DEUTSCH.DLL`, Version 4.76, ausgewählt.

## Bestätigt

- Reihenfolge der Hauptmenüs Datei, Bearbeiten, Bild, Optionen, Ansicht, Hilfe.
- Texte, Reihenfolgen und angezeigte Tastenkürzel der fünf angeforderten Hauptmenüs; Rohbeobachtungen in den jeweiligen `.uia.txt`-Dateien.
- Untermenüs Anzeige-Optionen, Farbkanal anzeigen, Negativ erstellen, Farben tauschen.
- Schwarzer Bildhintergrund, klassische Symbolleiste mit Zoomfeld, Statusbereiche mit Bildgröße/Farbtiefe, Dateiposition, Zoom, Speicher, Datum und Markierung/Seite.
- Größe ändern: zwei Spalten, Größeninformationen oben links, absolute Größe/Prozent/Megapixel, Pixel/cm/inch, Proportional, Schärfen, DPI-Anpassung, DPI-Eingabe, Standardmaße 640×480 bis 8K, Bildschirm-Varianten, Halb/Doppelt, Seiten vertauschen, eigene Standards, Resample/Resize, sechs Filter, schneller Verkleinerungsfilter und Gamma-Option.
- Größenänderung: Proportional an, Lanczos gewählt, schneller Verkleinerungsfilter an, Gamma/Schärfen/DPI-Anpassung aus. Deaktivierte Megapixel-Eingabe zeigt initial 4.00. DPI folgt in der beobachteten Vorlage 96.
- Farben ändern: Vorschauen Originalbild/Neues Bild, Helligkeit, Kontrast, Gamma, Sättigung, RGB-Balance, Profile mit Laden/Löschen/Speichern, Auf Bild anwenden, Standardwerte, Werte speichern, Weißabgleich-Hinweis.
- Neues Bild erzeugen: Breite/Höhe initial 100, X/Y-DPI 96, Farbtiefenradios 1/4/8/24/32 BPP, Transparenz und Graustufen, Hintergrundfarbe. Beobachtete Hintergrundfarbe orange.
- Fein-Rotation öffnet in dieser deutschen Installation tatsächlich den englischen PlugIn-Dialog „Rotate by angle“. Rohdaten dazu liegen vor.

## Offen oder abweichend

- Zugriffstasten der einzelnen Menüeinträge sind in `menus.de.json` bewusst `null`; UIA liefert sie nicht zuverlässig. Die fünf Hauptmenüs sind erreichbar, ihre vollständige Mnemonik ist noch nicht separat vermessen.
- Trennlinien der fotografierten Hauptmenüs wurden teilweise erfasst. Untermenü-Trennlinien und manche Optionsgruppen sind noch nicht vollständig strukturiert; dafür sind weitere Abgleiche nötig. `verification` an einem Eintrag bestätigt dessen Beobachtung, nicht sämtliche umgebenden Layoutdetails.
- Weitere Untermenüs (Effekte, Palette, Mehrseitigkeit, Dateisortierung, Markierungsformen, Einfügen, Favoriten, externe Editoren usw.) sind noch nicht vollständig erfasst.
- Exakte Pixelkoordinaten, DLU-Skalierung, Schriftmetriken und Darstellung bei 125–200 % DPI müssen noch vermessen werden.
- Eingabekopplung, Rundung bei ungeraden Dimensionen, DPI-Automatik bei cm/inch und genaue Wirkung des schnellen Verkleinerungsfilters wurden noch nicht durch Messreihen mit dem Original abgesichert. Die implementierten Formeln sind eigenständig und getestet.
- Die sechs Filter verwenden eigenständige mathematische Kerne. Ihre Ausgabe ist nicht als bitidentisch mit proprietären IrfanView-Algorithmen belegt.
- Speichern mit formatspezifischen Optionen, vollständiger Druckdialog, Stapeldialog, spezielle Markierung, Leinwand und Bildinformationen sind funktional eigene Qt-Dialoge. Ihr vollständiger Originalaufbau ist noch nicht nachgebildet.
- Farben ändern folgt der beobachteten Grundanordnung; Wertebereiche, Weißabgleichalgorithmus, Profilformat und Details der Vorschauen sind eigenständig.
- Das Hilfe-Menü ist IxView-spezifisch. Nicht implementierte Originalbefehle werden aus der benutzbaren Oberfläche ausgeblendet, bleiben aber in den Referenzdaten erhalten. Die komplette Originalstruktur ist deshalb noch nicht erreicht.

## Nächste gezielte Referenzmessungen

1. Größenänderung mit 403×201 Pixeln: Breite 100/101, Höhe 50/51, Prozent 33.33/66.67, Einheit cm/inch, DPI 72/96/300, Proportional aus/an und Seiten vertauschen.
2. Alle Einstellungsseiten und die zusätzlichen Untermenüs mit geladenem RGB-, RGBA-, GIF- und mehrseitigem TIFF-Bild.
3. JPEG-/PNG-/TIFF-/WebP-Speicheroptionen, EXIF-Anzeige und Metadatenerhalt nach Drehung, Zuschnitt und Neuencodierung.
4. Tastatur/Maus in Vollbild, bei Selektion und Navigation; Sortierregeln mit Ziffern, Umlauten und gleichen Zeitstempeln.
5. Screenshot-Paare bei gleicher Windows-Skalierung und Fenstergröße. Bisher liegt nur ein manueller Sichtabgleich vor; kein automatisierter Pixelvergleich zum Original.

## Umsetzung 0.2

Die Feinrotation verwendet nun den beobachteten englischen Titel, Optionen, Defaults und zwei Vorschauen. Winkellinie, Rechtsklick-Aktionen, Leinwand, Hintergrund, Qualität, innere Markierung und Schärfung sind implementiert. Die Hauptfenster-Farbpipette und das Paint-Plugin aus den Originalhinweisen sind nicht implementiert; diese Hinweise werden nicht angezeigt. Die Geometrie ist angenähert, die Rotationsalgorithmen sind eigenständig.

Zusätzliche verifizierte Bearbeiten-Einträge sind verbunden: ID 190 (letzte Markierung), 69 (maximieren/zentrieren), 185 (umgekehrte Markierung für Effekte), 192 (sichtbaren Fensterbereich freistellen). Die Einträge des separaten Verhältnis-Untermenüs bleiben unbekannt. Ein erneuter Zugriff auf die Referenz scheiterte am 18.10.2026 mit `GetCursorPos: Zugriff verweigert (0x80070005)`; die hier dokumentierten Beobachtungen stammen weiterhin vom 17.10.2026.

## Tastatur-/Mausreferenz 18.10.2026

Ergänzend wurden die [offiziellen Hotkeys](https://www.irfanview.net/help/guida/hlp_hotkeys.htm) für Auswahlzoom, Pfeiltasten, Kontextbelegungen und Tastaturaliase ausgewertet. Die zusätzlichen Regeln sind in `resources/shortcuts.de.json` mit Quelle getrennt von den lokal erfassten Menüs abgelegt. Ein vollständiger nativer Ablaufvergleich mit 4.76 steht weiterhin aus. Die öffentliche Produktbeschreibung lautet gemäß Nutzerwunsch: „Von IrfanView und nomacs inspiriert.“
