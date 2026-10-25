# Erweiterungen in üxzView 0.4

Der Manager unter **Optionen → Erweiterungen verwalten** wird zusammen mit Qt Network erst beim Öffnen geladen. Ohne ausdrückliches Laden eines Katalogs oder Installieren eines Pakets baut üxzView darüber keine Verbindung auf. Es gibt derzeit keinen voreingestellten öffentlichen Downloadserver. Für optionale Werkzeuge ist ein Katalog mit Versionen, Prüfsummen und tatsächlichen Paketgrößen eingebettet. Angezeigt werden mitgelieferte Qt-Bildformatplugins, installierte eigene Pakete und Einträge eines selbst gewählten Katalogs.

Unterstützt sind native **Qt-Image-I/O-Plugins**. Das ist eine eigene Paketschnittstelle, keine IrfanView-DLL- oder 8BF-Schnittstelle. QR-/Barcode-Lesen und Tesseract-Texterkennung stehen als separate Funktionserweiterungen bereit; Sprachpakete werden einzeln installiert. Installierte Funktionserweiterungen und Sprachpakete lassen sich über „Entfernen“ samt älteren Versionen löschen. Laufende Erkennungen schützen die verwendeten Pakete bis zum Abschluss. Details und Paketbau: [Function extensions](TOOL_EXTENSIONS.md). PDF-Dokument- und KI-Pluginverträge sind nicht implementiert. Ein Bildformatplugin kann zusätzliche durch QImageReader lesbare Formate bereitstellen, sofern seine Bibliotheken passen. Aus der Installation allein folgt kein Nachweis aller Formatvarianten.

## Benutzung

Der Manager trennt **Bildpaket-Formate** und **Funktionserweiterungen**. „Lokales Paket installieren“ liest `.ixplugin`, JSON oder `.ixtool`. Der oben angezeigte Erweiterungsordner lässt sich durch eine geprüfte Kopie und anschließenden Neustart wechseln. Die folgenden Angaben zu Qt-Kompatibilität und Neustart beziehen sich auf Bildformatpakete. „Katalog laden“ akzeptiert HTTPS und lokale `file:///`-Dateien. Die Suche filtert Namen, Formate und Status. Ein Katalogeintrag lässt sich installieren oder durch eine höhere Version aktualisieren. Betriebssystem, Prozessorarchitektur, Qt-Version, Qt-Pluginmetadaten und SHA-256 werden geprüft. Ein 32-Bit-Plugin wird in einer 64-Bit-Anwendung abgewiesen; es wird nicht umgewandelt. Für die Plattform muss ein passender Build vorhanden sein.

Installation, Update und Deaktivierung sind nach **Neustart** wirksam. Aktive Bibliotheken werden während der Sitzung nicht entladen. Alte Paketversionen bleiben für die Nachvollziehbarkeit liegen. Mitgelieferte Bildformate können über eine persistente Formatsperre deaktiviert werden; das löscht keine DLLs. „Alle Dateien“ umgeht diese Sperre nicht, da zusätzlich das vom Decoder erkannte Format geprüft wird.

Pakete enthalten ausführbaren nativen Code und müssen aus einer vertrauenswürdigen Quelle stammen. Hashes erkennen abweichende Downloads, bestätigen aber nicht die Vertrauenswürdigkeit eines Herausgebers. Es gibt keine Paket-Signaturinfrastruktur. Bibliotheken, Compilerlaufzeit und Qt-Build müssen ABI-kompatibel sein; die Prüfung von Version/Architektur ersetzt keinen Funktionstest. Lizenztexte müssen vom Herausgeber separat bereitgestellt werden.

## Paket erstellen

Beispiel für ein eigenes Manifest (Versionen an den tatsächlichen Plugin-Build anpassen):

```json
{
  "schema": 1,
  "kind": "qt-imageformats",
  "id": "example.codec",
  "name": "Example Image Codec",
  "version": "1.0.0",
  "os": "windows",
  "arch": "x86_64",
  "qtMajor": 6,
  "qtMinor": 8
}
```

`os`: `windows`, `linux` oder `macos`; `arch`: `x86_64`, `arm64`, auf macOS zusätzlich `universal`. Es wird genau eine Plugin-Hauptdatei und bis zu 31 native Abhängigkeiten erwartet. Dateien erhalten flache Namen innerhalb von `imageformats/`; Pfadtraversierung wird abgewiesen. JSON maximal 64 MiB, dekodierte Dateien zusammen maximal 48 MiB. Anstelle ungezielter System-DLL-Pakete nur benötigte eigene Abhängigkeiten einfügen.

```sh
python scripts/create_plugin_package.py --manifest codec.json \
  --plugin /path/to/qexample.dll --dependency /path/to/example.dll \
  --output example-1.0.0.ixplugin
```

Das Skript erzeugt `files` mit `name`, `role`, `sha256` und Base64-`data` und gibt den SHA-256 des Gesamtpakets aus. Es lädt keinen nativen Code. Die eigentliche Kompatibilitätsprüfung findet bei der Installation statt. Ein installiertes Paket liegt unter dem plattformeigenen AppLocalData-Verzeichnis `extensions/<id>/<version>/`; `state.json` bestimmt die für den nächsten Start aktive Version.

## Katalog

Ein Katalog ist JSON mit `schema: 1` und einem Array `packages`. Jeder Eintrag enthält die Manifestfelder ohne `files`, zusätzlich `packageUrl` und den SHA-256 des gesamten Pakets:

```json
{
  "schema": 1,
  "packages": [{
    "schema": 1, "kind": "qt-imageformats",
    "id": "example.codec", "name": "Example Image Codec", "version": "1.0.0",
    "os": "windows", "arch": "x86_64", "qtMajor": 6, "qtMinor": 8,
    "packageUrl": "example-1.0.0.ixplugin",
    "sha256": "HIER_DEN_64_STELLIGEN_HEX_SHA256_DES_PAKETS_EINSETZEN"
  }]
}
```

Relative Paketadressen werden gegen die Katalogadresse aufgelöst. HTTP ist nicht zulässig; Es sind höchstens fünf HTTPS-Weiterleitungen erlaubt, auch auf andere Download-Hosts; ein Wechsel von HTTPS zu HTTP bleibt gesperrt. Kataloge sind auf 4 MiB beschränkt, Downloads auf 64 MiB, mit Abbruch und 30-Sekunden-Transferzeitlimit. Es gibt keine Hintergrundupdates. Ein Update verwendet dieselbe Paketkennung mit einer neuen Version; die aktive Version wird atomar im Zustand umgestellt.
