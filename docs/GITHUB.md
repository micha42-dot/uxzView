# Projekt auf GitHub einrichten

Das Repository enthält Quellcode, Buildskripte, Tests, Dokumentation und GPL-3.0-or-later. **Fertige Programme gehören in Releases; `.deps`, `build`, `dist` und `tests/output` bleiben lokal.** Die ursprünglichen privaten Anforderungen und internen Arbeitsnotizen sind ebenfalls ausgeschlossen. `.gitattributes` hält Unix-Skripte beim Checkout auf LF.

## Öffentlichen Inhalt vorbereiten

### Mit dem separaten GitHub Updater

Der GitHub Updater ist ein eigenständiges Werkzeug im separaten Projektordner `Github-Updater`. Dort `Update GitHub.bat` öffnen, den üxzView-Projektordner hinzufügen und das Repository einmal einrichten. Danach Änderungen und lokale Commits prüfen und mit **Update hochladen** bestätigen. Die Einrichtung und die Tests des Hilfsprogramms sind in dessen eigener README beschrieben; es wird nicht mit üxzView ausgeliefert.

Die öffentlichen Quelldateien werden durch [`.github/github-updater.json`](../.github/github-updater.json) festgelegt. Der Quell-ZIP-Export verwendet dieselben Regeln. SDKs, Builds, Releases und persönliche Einstellungen bleiben ausgeschlossen. Der separate Updater liest diese Konfiguration, ohne Programmcode aus dem ausgewählten Projekt zu importieren. Andere Projekte verwenden ihre jeweilige `.gitignore`, sofern sie keine solche öffentliche Auswahl konfigurieren.

Die bestehende Projektliste und die Git-Anmeldung bleiben vom Umzug unberührt. Der Updater führt keine Builds oder Programmtests aus; dafür vorher den [Validierungslauf](DEVELOPMENT.md#build-and-verify) verwenden. Der reine üxzView-Inhaltscheck bleibt direkt im Projekt verfügbar:

### Manueller Inhaltscheck

Im Projektverzeichnis mit Python:

```powershell
python scripts/prepare_github.py
```

Das prüft die ausgewählten öffentlichen Dateien auf typische Zugangsdatenmuster, den konkreten lokalen Projektpfad und unerwartete Binärdateien. Der Prüfbericht liegt unter `build/github-manifest.json`. Das ist eine gezielte Prüfung, kein vollständiger Geheimnisscanner. Auf GitHub wird die Git-Arbeitskopie hochgeladen; falls ein Quell-ZIP benötigt wird, gibt es dafür ausschließlich das gemeinsame `dist/uxzView-source.zip` aus dem normalen Paketierungslauf.

## Repository anlegen und ersten Stand hochladen

Für ein neues Repository auf GitHub bietet sich der ASCII-Name `uxzView` an. Ein vorhandenes Repository kann auch unter seinem bisherigen Namen weiterverwendet werden. README, Lizenz und `.gitignore` beim Anlegen nicht automatisch erzeugen lassen; diese Dateien sind schon vorhanden. Die gewünschte Sichtbarkeit wählen.

Im lokalen Projekt oder im entpackten `uxzView-source`-Ordner:

```powershell
# Nur nötig, wenn noch kein lokales Git-Repository existiert:
git init -b main

git status --short
git add .
git diff --cached --stat
git diff --cached --check
git commit -m "Initial uxzView source release"

# ACCOUNT durch das tatsächliche GitHub-Konto ersetzen:
git remote add origin https://github.com/ACCOUNT/uxzView.git
git push -u origin main
```

Falls `origin` bereits existiert, erst mit `git remote -v` prüfen. Keine fremden Remotes überschreiben. Falls Git vor dem Commit nach einer Identität fragt, einen eigenen Namen und eine gewünschte E-Mail (gegebenenfalls die GitHub-noreply-Adresse) **lokal für dieses Repository** konfigurieren:

```powershell
git config user.name "DEIN NAME"
git config user.email "DEINE GIT-EMAIL"
```

Zum Push den üblichen GitHub-Anmeldedialog beziehungsweise SSH verwenden. Keine Tokens in Skripte, URLs oder Dateien schreiben. Die initiale Quellcode-Vorbereitung erfordert keine GitHub-Zugangsdaten.

## GitHub nach dem Upload

Unter Actions läuft **Desktop builds** bei Push und Pull Request: Windows x64, Linux x64, macOS arm64 und macOS Intel. Die Dateien unter Actions → Artifacts sind Entwicklungspakete. Sie werden nicht zu öffentlichen Releases. Den ersten tatsächlichen CI-Lauf auf allen Plattformen prüfen; vorbereitete Jobs belegen noch keine Portabilität.

Optional einen Schutz für `main` mit erfolgreichen Build-Jobs und Pull-Request-Review aktivieren. Unter Settings → Code security die private Schwachstellenmeldung aktivieren, wenn verfügbar. Issue-Formulare, PR-Vorlage und monatliche Dependabot-Prüfung der GitHub-Actions liegen bei.

Den ersten fertigen **Windows-Release** anhand [RELEASING.md](RELEASING.md) erstellen. Linux-/macOS-Binärdateien erst anbieten, wenn dortige Builds und native Tests abgeschlossen sind.

GitHub beschreibt den [Import lokalen Codes](https://docs.github.com/en/migrations/importing-source-code/using-the-command-line-to-import-source-code/adding-locally-hosted-code-to-github) und die [Berechtigungen von Workflows](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#permissions).
