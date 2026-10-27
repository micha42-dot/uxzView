// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/dialog_layout.h"
#include "core/localization.h"
#include "core/plugin_store.h"
#include "core/tool_store.h"
#include "core/extension_storage.h"
#include <QtConcurrent>
#include <QTemporaryFile>
#include <atomic>
#include <QVersionNumber>
#include <QtNetwork>
#include <QtWidgets>
namespace {
class Manager : public QDialog {
  public:
    Manager(QWidget *parent, const QString &tool = {}) : QDialog(parent) {
        setObjectName(QStringLiteral("extensionManagerDialog"));
        setProperty("httpsAvailable", QSslSocket::supportsSsl());
        setWindowTitle(IX_TEXT("üxzView – Erweiterungen"));
        resize(980, 610);
        auto *layout = ix::dialogLayout(this);
        auto *rootRow = new QHBoxLayout;
        root_ = new QLineEdit(ix::ExtensionStorage::requestedRoot());
        root_->setObjectName(QStringLiteral("extensionRoot"));
        root_->setReadOnly(true);
        root_->setCursorPosition(0);
        root_->setToolTip(root_->text());
        rootRow->addWidget(new QLabel(IX_TEXT("Erweiterungsordner:")));
        rootRow->addWidget(root_, 1);
        choose_ = new QPushButton(IX_TEXT("Ändern …"));
        choose_->setObjectName(QStringLiteral("chooseExtensionRoot"));
        rootRow->addWidget(choose_);
        auto *open = new QPushButton(IX_TEXT("Ordner öffnen"));
        rootRow->addWidget(open);
        layout->addLayout(rootRow);
        connect(open, &QPushButton::clicked, this, [this] {
            QDir().mkpath(root_->text());
            QDesktopServices::openUrl(QUrl::fromLocalFile(root_->text()));
        });
        connect(choose_, &QPushButton::clicked, this, [this] {
            const auto path = QFileDialog::getExistingDirectory(this, IX_TEXT("Leeren Erweiterungsordner auswählen"), QFileInfo(root_->text()).absolutePath());
            const auto selectedDomain = domain();
            if (!path.isEmpty()) work([this, path, selectedDomain] { return ix::ExtensionStorage::prepareRoot(path, cancel_, selectedDomain); },
                IX_TEXT("Erweiterungen kopiert. Der neue Ordner wird nach einem Neustart verwendet. Originale bleiben erhalten."));
        });
        auto *row = new QHBoxLayout;
        url_ = new QLineEdit;
        url_->setObjectName(QStringLiteral("extensionCatalogUrl"));
        url_->setPlaceholderText(IX_TEXT("HTTPS-Adresse oder file:///… eines üxzView-Katalogs"));
        url_->setText(QSettings().value(QStringLiteral("extensions/catalog")).toString());
        row->addWidget(url_, 1);
        auto *fetch = new QPushButton(IX_TEXT("Katalog laden"));
        fetch_ = fetch;
        row->addWidget(fetch);
        layout->addLayout(row);
        search_ = new QLineEdit;
        search_->setPlaceholderText(IX_TEXT("Erweiterungen / Formate suchen …"));
        layout->addWidget(search_);
        table_ = new QTableWidget(0, 4);
        table_->setObjectName(QStringLiteral("extensionTable"));
        table_->setHorizontalHeaderLabels({IX_TEXT("Erweiterung"), IX_TEXT("Version / Formate"),
                                           IX_TEXT("Status"), IX_TEXT("Kompatibilität")});
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setSelectionBehavior(QAbstractItemView::SelectRows);
        table_->setSelectionMode(QAbstractItemView::SingleSelection);
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        tabs_ = new QTabWidget;
        tabs_->setObjectName(QStringLiteral("extensionSections"));
        tabs_->addTab(table_, IX_TEXT("Bildpaket-Formate"));
        tools_ = new QTableWidget(0, 5);
        tools_->setObjectName(QStringLiteral("functionExtensionTable"));
        tools_->setHorizontalHeaderLabels({IX_TEXT("Erweiterung"), IX_TEXT("Version"), IX_TEXT("Status"), IX_TEXT("Download / installiert"), IX_TEXT("Kompatibilität")});
        tools_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        tools_->setSelectionBehavior(QAbstractItemView::SelectRows);
        tools_->setSelectionMode(QAbstractItemView::SingleSelection);
        tools_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
        tools_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        tabs_->addTab(tools_, IX_TEXT("Funktionserweiterungen"));
        layout->addWidget(tabs_, 1);
        status_ = new QLabel;
        status_->setWordWrap(true);
        status_->setObjectName(QStringLiteral("extensionStatus"));
        layout->addWidget(status_);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
        buttons->button(QDialogButtonBox::Close)->setText(IX_TEXT("Schließen"));
        local_ = buttons->addButton(IX_TEXT("Lokales Paket installieren …"), QDialogButtonBox::ActionRole);
        local_->setObjectName(QStringLiteral("installLocalExtension"));
        install_ = buttons->addButton(IX_TEXT("Installieren / aktualisieren"), QDialogButtonBox::ActionRole);
        install_->setObjectName(QStringLiteral("installCatalogExtension"));
        toggle_ = buttons->addButton(IX_TEXT("Aktivieren / deaktivieren"), QDialogButtonBox::ActionRole);
        toggle_->setObjectName(QStringLiteral("toggleExtension"));
        remove_ = buttons->addButton(IX_TEXT("Entfernen"), QDialogButtonBox::ActionRole);
        remove_->setObjectName(QStringLiteral("removeExtension"));
        connect(remove_, &QPushButton::clicked, this, [this] {
            const auto item = selected();
            if (tabs_->currentIndex() != 1 || item.value(QStringLiteral("origin")).toString() != QStringLiteral("installed")) return;
            const auto id = item.value(QStringLiteral("id")).toString();
            work([this, id] { return toolStore_.remove(id, cancel_); }, IX_TEXT("Entfernt."));
        });
        abort_ = buttons->addButton(IX_TEXT("Abbrechen"), QDialogButtonBox::ActionRole);
        abort_->setEnabled(false);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        connect(search_, &QLineEdit::textChanged, this, [this] { reload(); });
        connect(local_, &QPushButton::clicked, this, [this] {
            auto path = QFileDialog::getOpenFileName(this, QStringLiteral("üxzView-Paket"), {},
                                                     QStringLiteral("üxzView-Pakete (*.ixtool *.ixplugin *.json)"));
            if (path.isEmpty())
                return;
            QFile file(path);
            if (file.open(QIODevice::ReadOnly) && file.peek(9) == QByteArray("UXZTOOL1\n")) {
                installTool(path, {}); return;
            }
            file.close();
            if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
                status_->setText(IX_TEXT("Paket nicht lesbar oder größer als 64 MiB."));
                return;
            }
            installPackage(file.readAll(), {});
        });
        connect(fetch, &QPushButton::clicked, this, [this] {
            auto address = QUrl::fromUserInput(url_->text());
            if (!allowed(address)) {
                status_->setText(IX_TEXT("Katalog benötigt HTTPS oder eine lokale Datei."));
                return;
            }
            QSettings().setValue(QStringLiteral("extensions/catalog"), address.toString());
            download(address, 4 * 1024 * 1024, [this, address](QByteArray bytes) {
                QJsonParseError error;
                auto document = QJsonDocument::fromJson(bytes, &error);
                if (error.error != QJsonParseError::NoError || !document.isObject() ||
                    document.object().value(QStringLiteral("schema")).toInt() != 1 ||
                    !document.object().value(QStringLiteral("packages")).isArray()) {
                    status_->setText(IX_TEXT("Ungültiger üxzView-Katalog."));
                    return;
                }
                catalog_ = document.object().value(QStringLiteral("packages")).toArray();
                base_ = address;
                reload();
                status_->setText(
                    IX_TEXT("Katalog geladen; passende Aktualisierungen können ausgewählt werden."));
            });
        });
        connect(install_, &QPushButton::clicked, this, [this] {
            auto item = selected();
            if (tabs_->currentIndex() == 1) { obtainTool(item); return; }
            if (item.value(QStringLiteral("origin")).toString() != QStringLiteral("catalog"))
                return;
            auto error = ix::PluginStore::compatibility(item);
            if (!error.isEmpty()) {
                status_->setText(error);
                return;
            }
            auto address = base_.resolved(QUrl(item.value(QStringLiteral("packageUrl")).toString()));
            auto hash = item.value(QStringLiteral("sha256")).toString();
            if (!allowed(address) ||
                !QRegularExpression(QStringLiteral("^[a-fA-F0-9]{64}$")).match(hash).hasMatch()) {
                status_->setText(IX_TEXT("Paket benötigt eine HTTPS-/Datei-Adresse und SHA-256."));
                return;
            }
            download(address, 64 * 1024 * 1024, [this, hash, item](QByteArray bytes) {
                auto manifest = QJsonDocument::fromJson(bytes).object();
                for (auto key : {"id", "version", "os", "arch", "kind", "qtMajor", "qtMinor"})
                    if (manifest.value(QString::fromLatin1(key)) != item.value(QString::fromLatin1(key))) {
                        status_->setText(IX_TEXT("Paketbeschreibung stimmt nicht mit dem Katalog überein."));
                        return;
                    }
                installPackage(bytes, hash);
            });
        });
        connect(toggle_, &QPushButton::clicked, this, [this] {
            auto item = selected();
            if (tabs_->currentIndex() == 1) {
                if (item.value(QStringLiteral("origin")).toString() != QStringLiteral("installed")) return;
                const auto error = toolStore_.setEnabled(item.value(QStringLiteral("id")).toString(), !item.value(QStringLiteral("enabled")).toBool());
                status_->setText(error.isEmpty() ? IX_TEXT("Gespeichert.") : ix::localized(error));
                reload(); return;
            }
            auto origin = item.value(QStringLiteral("origin")).toString();
            QString error;
            if (origin == QStringLiteral("installed"))
                error = store_.setEnabled(item.value(QStringLiteral("id")).toString(),
                                          !item.value(QStringLiteral("enabled")).toBool());
            else if (origin == QStringLiteral("builtin")) {
                QStringList formats;
                bool enabled = false;
                for (auto f : item.value(QStringLiteral("formats")).toArray()) {
                    formats.append(f.toString());
                    enabled |= !store_.disabledFormats().contains(f.toString());
                }
                error = store_.setBuiltinEnabled(formats, !enabled);
            } else
                return;
            status_->setText(error.isEmpty() ? IX_TEXT("Gespeichert. Bitte üxzView neu starten.") : error);
            reload();
        });
        connect(abort_, &QPushButton::clicked, this, [this] {
            cancel_.store(true);
            if (reply_)
                reply_->abort();
        });
        connect(tabs_, &QTabWidget::currentChanged, this, [this] { updateRoot(); updateButtons(); });
        connect(table_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
        connect(tools_, &QTableWidget::itemSelectionChanged, this, [this] { updateButtons(); });
        connect(&worker_, &QFutureWatcher<QString>::finished, this, [this] {
            const auto error = worker_.result(); busy_ = false;
            updateRoot();
            reload(); status_->setText(error.isEmpty() ? success_ : ix::localized(error));
        });
        QFile catalog(QStringLiteral(":/tool-catalog.json"));
        if (catalog.open(QIODevice::ReadOnly)) bundled_ = QJsonDocument::fromJson(catalog.readAll()).object().value(QStringLiteral("packages")).toArray();
        reload();
        if (!tool.isEmpty()) {
            tabs_->setCurrentIndex(1);
            for (int row = 0; row < tools_->rowCount(); ++row)
                if (tools_->item(row, 0)->data(Qt::UserRole).toJsonObject().value(QStringLiteral("id")).toString() == tool) {
                    tools_->selectRow(row);
                    tools_->scrollToItem(tools_->item(row, 0));
                    break;
                }
        }
        status_->setText(ix::ExtensionStorage::restartPending(domain()) ? IX_TEXT("Neuer Erweiterungsordner vorgemerkt. Bitte üxzView neu starten.") : QString());
    }
    ~Manager() override {
        disconnect(&worker_, nullptr, this, nullptr);
        cancel_.store(true);
        if (reply_) {
            disconnect(reply_, nullptr, this, nullptr);
            reply_->abort();
        }
        worker_.waitForFinished();
    }

  private:
    ix::PluginStore store_;
    ix::ToolStore toolStore_;
    QFutureWatcher<QString> worker_;
    std::atomic_bool cancel_{false};
    bool busy_ = false;
    QString success_;
    QJsonArray bundled_;
    QLineEdit *root_;
    QTabWidget *tabs_;
    QTableWidget *tools_;
    QPushButton *choose_, *fetch_;
    QNetworkAccessManager network_;
    QNetworkReply *reply_ = nullptr;
    QJsonArray catalog_;
    QUrl base_;
    QLineEdit *url_, *search_;
    QTableWidget *table_;
    QLabel *status_;
    QPushButton *local_, *install_, *toggle_, *remove_, *abort_;
    static bool allowed(const QUrl &url) {
        return url.isValid() &&
               (url.scheme() == QStringLiteral("https") ||
                (url.isLocalFile() && (url.host().isEmpty() || url.host() == QStringLiteral("localhost"))));
    }
    QJsonObject selected() const {
        const auto *current = tabs_->currentIndex() == 1 ? tools_ : table_;
        return current->currentRow() < 0
                   ? QJsonObject()
                   : current->item(current->currentRow(), 0)->data(Qt::UserRole).toJsonObject();
    }
    ix::ExtensionStorage::Domain domain() const {
        return tabs_->currentIndex() == 1 ? ix::ExtensionStorage::Domain::Tools : ix::ExtensionStorage::Domain::Formats;
    }
    void updateRoot() {
        root_->setText(ix::ExtensionStorage::requestedRoot(domain()));
        root_->setCursorPosition(0);
        root_->setToolTip(root_->text());
    }
    void updateButtons() {
        const bool running = busy_ || reply_;
        const bool mutableRoot = !running && !ix::ExtensionStorage::restartPending(domain());
        const auto origin = selected().value(QStringLiteral("origin")).toString();
        local_->setEnabled(mutableRoot);
        choose_->setEnabled(mutableRoot);
        fetch_->setEnabled(!running);
        install_->setEnabled(mutableRoot && origin == QStringLiteral("catalog"));
        toggle_->setEnabled(mutableRoot && (origin == QStringLiteral("installed") || origin == QStringLiteral("builtin")));
        remove_->setVisible(tabs_->currentIndex() == 1);
        remove_->setEnabled(mutableRoot && origin == QStringLiteral("installed"));
        abort_->setEnabled(running);
        tabs_->setEnabled(!busy_);
    }
    void work(std::function<QString()> task, const QString &success) {
        if (busy_ || reply_ || ix::ExtensionStorage::restartPending(domain())) return;
        busy_ = true; cancel_.store(false); success_ = success;
        status_->setText(IX_TEXT("Erweiterungen werden vorbereitet …"));
        updateButtons();
        worker_.setFuture(QtConcurrent::run([task = std::move(task)] {
            try { return task(); }
            catch (const std::exception &exception) { return QString::fromUtf8(exception.what()); }
            catch (...) { return IX_TEXT("Vorgang fehlgeschlagen."); }
        }));
    }
    void installTool(const QString &path, const QJsonObject &expected, std::shared_ptr<QTemporaryFile> keep = {}) {
        work([this, path, expected, keep] { return toolStore_.install(path, expected, cancel_); }, IX_TEXT("Werkzeugpaket installiert."));
    }
    void obtainTool(const QJsonObject &item) {
        if (item.value(QStringLiteral("origin")).toString() != QStringLiteral("catalog")) return;
        const auto error = ix::ToolStore::compatibility(item);
        if (!error.isEmpty()) { status_->setText(ix::localized(error)); return; }
        const auto url = QUrl(item.value(QStringLiteral("catalogBase")).toString()).resolved(QUrl(item.value(QStringLiteral("packageUrl")).toString()));
        if (url.isLocalFile()) {
            auto path = url.toLocalFile();
            if (!QFileInfo::exists(path)) path = QFileDialog::getOpenFileName(this, IX_TEXT("Werkzeugpaket auswählen"), {}, QStringLiteral("üxzView (*.ixtool)"));
            if (!path.isEmpty()) installTool(path, item);
            return;
        }
        if (!allowed(url)) { status_->setText(IX_TEXT("Paket benötigt HTTPS oder eine lokale Datei.")); return; }
        const qint64 size = item.value(QStringLiteral("bytes")).toInteger();
        if (size <= 0 || size > 512LL * 1024 * 1024 || !QRegularExpression(QStringLiteral("^[a-fA-F0-9]{64}$")).match(item.value(QStringLiteral("sha256")).toString()).hasMatch()) {
            status_->setText(IX_TEXT("Downloadgröße oder Paketprüfsumme fehlt oder stimmt nicht.")); return;
        }
        auto file = std::make_shared<QTemporaryFile>();
        if (!file->open()) { status_->setText(file->errorString()); return; }
        QNetworkRequest request(url);
        request.setTransferTimeout(30000);
        // Release hosts may redirect to a separate HTTPS asset CDN. Forbid
        // downgrades while keeping those signed downloads usable.
        request.setMaximumRedirectsAllowed(5);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        reply_ = network_.get(request);
        auto *reply = reply_;
        reply->setReadBufferSize(1024 * 1024);
        auto failed = std::make_shared<bool>(false);
        auto drain = [reply, file, failed, size] {
            while (reply->bytesAvailable() > 0 && !*failed) {
                const auto bytes = reply->read(1024 * 1024);
                if (bytes.isEmpty() || file->size() + bytes.size() > size || file->write(bytes) != bytes.size()) { *failed = true; reply->abort(); }
            }
        };
        connect(reply, &QIODevice::readyRead, this, drain);
        connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 current, qint64 total) {
            status_->setText(IX_TEXT("Download: ") + QStringLiteral("%1 / %2 MiB").arg(double(current) / 1048576, 0, 'f', 2).arg(double(total) / 1048576, 0, 'f', 2));
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, file, failed, drain, item, size] {
            drain(); reply_ = nullptr; updateButtons();
            if (*failed || file->size() != size) status_->setText(IX_TEXT("Downloadgröße stimmt nicht oder Datei ist nicht speicherbar."));
            else if (reply->error() != QNetworkReply::NoError) status_->setText(reply->errorString());
            else if (!file->flush()) status_->setText(file->errorString());
            else installTool(file->fileName(), item, file);
            reply->deleteLater();
        });
        updateButtons(); status_->setText(IX_TEXT("Download läuft …"));
    }
    void installPackage(const QByteArray &bytes, const QString &hash) {
        auto error = store_.install(bytes, hash);
        status_->setText(error.isEmpty() ? IX_TEXT("Paket installiert. Bitte üxzView neu starten.") : error);
        reload();
    }
    void download(const QUrl &url, qint64 limit, std::function<void(QByteArray)> done) {
        if (busy_ || reply_) {
            status_->setText(IX_TEXT("Ein Download läuft bereits."));
            return;
        }
        if (url.isLocalFile()) {
            QFile file(url.toLocalFile());
            if (!file.open(QIODevice::ReadOnly) || file.size() > limit) {
                status_->setText(IX_TEXT("Datei nicht lesbar oder zu groß."));
                return;
            }
            done(file.readAll());
            return;
        }
        QNetworkRequest request(url);
        request.setTransferTimeout(30000);
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        request.setMaximumRedirectsAllowed(5);
        reply_ = network_.get(request);
        auto *reply = reply_;
        auto bytes = std::make_shared<QByteArray>();
        auto tooLarge = std::make_shared<bool>(false);
        updateButtons();
        status_->setText(IX_TEXT("Download läuft …"));
        connect(reply, &QIODevice::readyRead, this, [reply, bytes, tooLarge, limit] {
            if (*tooLarge)
                return;
            bytes->append(reply->readAll());
            if (bytes->size() > limit) {
                *tooLarge = true;
                bytes->clear();
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this,
                [this, reply, bytes, tooLarge, limit, done = std::move(done)] {
                    bytes->append(reply->readAll());
                    reply_ = nullptr;
                    updateButtons();
                    if (*tooLarge || bytes->size() > limit)
                        status_->setText(IX_TEXT("Download überschreitet die Größenbegrenzung."));
                    else if (reply->error() != QNetworkReply::NoError)
                        status_->setText(reply->errorString());
                    else
                        done(*bytes);
                    reply->deleteLater();
                });
    }
    void reload() {
        reloadTools();
        table_->setRowCount(0);
        auto installed = store_.installed();
        QHash<QString, QString> versions;
        for (auto value : installed) {
            auto item = value.toObject();
            versions.insert(item.value(QStringLiteral("id")).toString(),
                            item.value(QStringLiteral("version")).toString());
        }
        auto add = [this](QJsonObject item, const QString &name, const QString &version,
                          const QString &status, const QString &compatibility) {
            if (!(name + version + status).contains(search_->text(), Qt::CaseInsensitive))
                return;
            int r = table_->rowCount();
            table_->insertRow(r);
            QStringList cells{name, version, status, compatibility};
            for (int c = 0; c < 4; ++c) {
                auto *cell = new QTableWidgetItem(cells[c]);
                cell->setData(Qt::UserRole, item);
                table_->setItem(r, c, cell);
            }
        };
        for (auto value : installed) {
            auto item = value.toObject();
            item.insert(QStringLiteral("origin"), QStringLiteral("installed"));
            add(item, item.value(QStringLiteral("id")).toString(),
                item.value(QStringLiteral("version")).toString(),
                item.value(QStringLiteral("enabled")).toBool() ? IX_TEXT("Aktiv nach Neustart")
                                                               : IX_TEXT("Deaktiviert nach Neustart"),
                ix::PluginStore::compatibility(item));
        }
        for (auto value : ix::PluginStore::builtins()) {
            auto item = value.toObject();
            item.insert(QStringLiteral("origin"), QStringLiteral("builtin"));
            QStringList formats;
            bool enabled = false;
            for (auto f : item.value(QStringLiteral("formats")).toArray()) {
                formats.append(f.toString());
                enabled |= !store_.disabledFormats().contains(f.toString());
            }
            add(item, item.value(QStringLiteral("name")).toString(), formats.join(QStringLiteral(", ")),
                enabled ? IX_TEXT("Mitgeliefert · aktiv") : IX_TEXT("Mitgeliefert · deaktiviert"), QString());
        }
        for (auto value : catalog_) {
            auto item = value.toObject();
            if (item.value(QStringLiteral("kind")).toString() != QStringLiteral("qt-imageformats")) continue;
            item.insert(QStringLiteral("origin"), QStringLiteral("catalog"));
            auto id = item.value(QStringLiteral("id")).toString(),
                 version = item.value(QStringLiteral("version")).toString();
            auto status =
                versions.contains(id)
                    ? (QVersionNumber::fromString(version) > QVersionNumber::fromString(versions.value(id))
                           ? IX_TEXT("Aktualisierung verfügbar")
                           : IX_TEXT("Bereits installiert / ältere Version"))
                    : IX_TEXT("Verfügbar");
            add(item, item.value(QStringLiteral("name")).toString(id), version, status,
                ix::PluginStore::compatibility(item));
        }
        updateButtons();
    }
    void reloadTools() {
        tools_->setRowCount(0);
        QHash<QString, QString> versions;
        auto add = [this](QJsonObject item, const QString &status, qint64 download, qint64 installed) {
            auto name = item.value(QStringLiteral("name")).toString(item.value(QStringLiteral("id")).toString());
            const auto language = item.value(QStringLiteral("language")).toString();
            const QMap<QString, QString> names{{QStringLiteral("deu"), IX_TEXT("Deutsch")}, {QStringLiteral("eng"), IX_TEXT("Englisch")},
                {QStringLiteral("fra"), IX_TEXT("Französisch")}, {QStringLiteral("jpn"), IX_TEXT("Japanisch")}, {QStringLiteral("kor"), IX_TEXT("Koreanisch")}};
            if (!language.isEmpty()) name = QStringLiteral("OCR: ") + names.value(language, language);
            if (!(name + status).contains(search_->text(), Qt::CaseInsensitive)) return;
            auto size = [](qint64 value) { return value > 0 ? QStringLiteral("%1 MiB").arg(double(value) / 1048576, 0, 'f', 2) : QStringLiteral("—"); };
            const QStringList cells{name, item.value(QStringLiteral("version")).toString(), status,
                size(download) + QStringLiteral(" / ") + size(installed), ix::ToolStore::compatibility(item)};
            const int row = tools_->rowCount(); tools_->insertRow(row);
            for (int col = 0; col < cells.size(); ++col) { auto *cell = new QTableWidgetItem(cells[col]); cell->setData(Qt::UserRole, item); tools_->setItem(row, col, cell); }
        };
        for (const auto &value : toolStore_.installed()) {
            auto item = value.toObject(); item.insert(QStringLiteral("origin"), QStringLiteral("installed"));
            versions.insert(item.value(QStringLiteral("id")).toString(), item.value(QStringLiteral("version")).toString());
            qint64 bytes = 0;
            for (const auto &entry : item.value(QStringLiteral("files")).toArray()) bytes += entry.toObject().value(QStringLiteral("bytes")).toInteger();
            add(item, item.value(QStringLiteral("enabled")).toBool() ? IX_TEXT("Aktiv") : IX_TEXT("Deaktiviert"), 0, bytes);
        }
        auto all = bundled_;
        const auto app = QCoreApplication::applicationDirPath();
        auto local = app + QStringLiteral("/../tools/uxzView-tools.json");
        QDir checkout(app);
        for (int depth = 0; !QFileInfo::exists(local) && depth < 3; ++depth) {
            const auto candidate = checkout.filePath(QStringLiteral("dist/tools/uxzView-tools.json"));
            if (QFileInfo::exists(candidate)) { local = candidate; break; }
            if (!checkout.cdUp()) break;
        }
        for (qsizetype i = 0; i < all.size(); ++i) { auto item = all[i].toObject(); item.insert(QStringLiteral("catalogBase"), QUrl::fromLocalFile(local).toString()); all[i] = item; }
        for (const auto &value : catalog_) {
            auto item = value.toObject();
            const auto kind = item.value(QStringLiteral("kind")).toString();
            if (kind != QStringLiteral("image-tool") && kind != QStringLiteral("ocr-language")) continue;
            // Prefer a hosted entry over bundled metadata for the same package.
            for (qsizetype i = all.size(); i > 0; --i)
                if (all[i - 1].toObject().value(QStringLiteral("id")) == item.value(QStringLiteral("id"))) all.removeAt(i - 1);
            item.insert(QStringLiteral("catalogBase"), base_.toString()); all.append(item);
        }
        for (const auto &value : all) {
            auto item = value.toObject(); const auto id = item.value(QStringLiteral("id")).toString();
            if (versions.value(id) == item.value(QStringLiteral("version")).toString()) continue;
            item.insert(QStringLiteral("origin"), QStringLiteral("catalog"));
            add(item, versions.contains(id) ? IX_TEXT("Aktualisierung verfügbar") : IX_TEXT("Verfügbar"),
                item.value(QStringLiteral("bytes")).toInteger(), item.value(QStringLiteral("installedBytes")).toInteger());
        }
    }
};
} // namespace
extern "C" Q_DECL_EXPORT void ixOpenExtensionManager(QWidget *parent) {
    ix::setUiLanguage(qApp->property("ixUiLanguage").toString());
    Manager dialog(parent);
    dialog.exec();
}
extern "C" Q_DECL_EXPORT void ixOpenFunctionExtensionManager(QWidget *parent, const char *tool) {
    ix::setUiLanguage(qApp->property("ixUiLanguage").toString());
    Manager dialog(parent, QString::fromUtf8(tool));
    dialog.exec();
}
