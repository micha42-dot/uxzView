// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_dialog.h"
#include "core/file_journal.h"
#include "core/image_catalog.h"
#include "core/localization.h"
#include "core/preview_service.h"
#include "icons.h"
#include <QScopeGuard>
#include <QtConcurrent>
#include <QtWidgets>
#include <algorithm>
namespace ix {
namespace {
class ViewportObserver final : public QObject {
  public:
    ViewportObserver(QWidget *viewport, std::function<void()> changed)
        : QObject(viewport), changed_(std::move(changed)) {
        viewport->installEventFilter(this);
    }
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show)
            changed_();
        return false;
    }

  private:
    std::function<void()> changed_;
};
} // namespace
QString undoFileOperation(QWidget *parent, const QString &id, const QString &journalRoot) {
    QProgressDialog progress(IX_TEXT("Dateioperation wird rückgängig gemacht …"), {}, 0, 0, parent);
    progress.setCancelButton(nullptr);
    progress.setWindowModality(Qt::WindowModal);
    QFutureWatcher<QString> watcher;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &progress, &QDialog::accept);
    watcher.setFuture(QtConcurrent::run([id, journalRoot] { return FileJournal(journalRoot).undo(id); }));
    progress.exec();
    return watcher.result();
}
void journalDialog(QWidget *parent, const QString &journalRoot) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("fileJournalDialog"));
    dialog.setWindowTitle(IX_TEXT("Dateijournal und interner Papierkorb"));
    dialog.resize(1000, 560);
    auto *layout = new QVBoxLayout(&dialog);
    auto *note = new QLabel(IX_TEXT("Löschen verschiebt Dateien in den internen Papierkorb. Rückgängig "
                                    "überschreibt keine inzwischen geänderten Dateien. Ausstehende Einträge "
                                    "nach einem Abbruch bitte anhand der angezeigten Pfade prüfen."));
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *storageStatus = new QLabel;
    storageStatus->setWordWrap(true);
    layout->addWidget(storageStatus);
    auto *table = new QTableWidget(0, 5);
    table->setObjectName(QStringLiteral("fileJournalTable"));
    table->setHorizontalHeaderLabels({IX_TEXT("Zeit"), IX_TEXT("Aktion"), IX_TEXT("Status"),
                                      IX_TEXT("Quelle"), IX_TEXT("Ziel / Papierkorb")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(table);
    auto reload = [table, storageStatus, journalRoot] {
        table->setRowCount(0);
        QString error;
        const auto entries = FileJournal(journalRoot).entries(&error);
        storageStatus->setText(localized(error));
        for (int n = entries.size() - 1; n >= 0; --n) {
            auto o = entries[n].toObject();
            int r = table->rowCount();
            table->insertRow(r);
            int col = 0;
            for (const auto &key : {"time", "kind", "state", "source", "target"}) {
                auto *item = new QTableWidgetItem(o.value(QString::fromLatin1(key)).toString());
                item->setData(Qt::UserRole, o.value(QStringLiteral("id")).toString());
                table->setItem(r, col++, item);
            }
        }
        table->resizeColumnsToContents();
    };
    reload();
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *restore =
        buttons->addButton(IX_TEXT("Ausgewählte Operation rückgängig"), QDialogButtonBox::ActionRole);
    auto *folder = buttons->addButton(IX_TEXT("Papierkorb öffnen"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    QObject::connect(restore, &QPushButton::clicked, &dialog, [&] {
        int r = table->currentRow();
        if (r < 0)
            return;
        auto error = undoFileOperation(&dialog, table->item(r, 0)->data(Qt::UserRole).toString(), journalRoot);
        if (!error.isEmpty())
            QMessageBox::warning(&dialog, IX_TEXT("Dateijournal"), error);
        reload();
    });
    QObject::connect(folder, &QPushButton::clicked, &dialog, [journalRoot] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(FileJournal(journalRoot).root() + QStringLiteral("/trash")));
    });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
void libraryDialog(QWidget *parent, const QStringList &files, const std::function<void(QString)> &open, const QString &journalRoot) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("libraryDialog"));
    dialog.setWindowTitle(IX_TEXT("Bildverwaltung"));
    dialog.resize(1060, 700);
    auto *layout = new QVBoxLayout(&dialog);
    auto *filters = new QHBoxLayout;
    auto *search = new QLineEdit;
    search->setObjectName(QStringLiteral("catalogSearch"));
    search->setPlaceholderText(IX_TEXT("Dateiname oder indexierte Metadaten suchen …"));
    filters->addWidget(search, 1);
    auto *minimum = new QSpinBox;
    minimum->setRange(0, 5);
    minimum->setPrefix(IX_TEXT("Mind. Sterne: "));
    filters->addWidget(minimum);
    auto *color = new QComboBox;
    const QStringList colors{QString(),
                             QStringLiteral("red"),
                             QStringLiteral("yellow"),
                             QStringLiteral("green"),
                             QStringLiteral("blue"),
                             QStringLiteral("purple")};
    const QStringList labels{IX_TEXT("Alle Farben"), IX_TEXT("Rot"),  IX_TEXT("Gelb"),
                             IX_TEXT("Grün"),        IX_TEXT("Blau"), IX_TEXT("Violett")};
    color->addItems(labels);
    filters->addWidget(color);
    layout->addLayout(filters);
    auto *list = new QListWidget;
    list->setObjectName(QStringLiteral("catalogImages"));
    list->setViewMode(QListView::IconMode);
    list->setResizeMode(QListView::Adjust);
    list->setMovement(QListView::Static);
    list->setIconSize({128, 96});
    list->setGridSize({170, 140});
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setUniformItemSizes(true);
    layout->addWidget(list, 1);
    auto *annotation = new QHBoxLayout;
    auto *rating = new QSpinBox;
    rating->setRange(0, 5);
    rating->setPrefix(IX_TEXT("Bewertung: "));
    annotation->addWidget(rating);
    auto *mark = new QComboBox;
    auto markLabels = labels;
    markLabels[0] = IX_TEXT("Keine Farbmarkierung");
    mark->addItems(markLabels);
    annotation->addWidget(mark);
    auto *save = new QPushButton(IX_TEXT("Auf Auswahl anwenden"));
    annotation->addWidget(save);
    annotation->addStretch();
    layout->addLayout(annotation);
    auto *status = new QLabel;
    status->setObjectName(QStringLiteral("catalogStatus"));
    status->hide();
    status->setWordWrap(true);
    layout->addWidget(status);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close);
    auto *index = buttons->addButton(IX_TEXT("Metadaten indexieren"), QDialogButtonBox::ActionRole);
    auto *exact = buttons->addButton(IX_TEXT("Identische Bilder"), QDialogButtonBox::ActionRole);
    auto *near = buttons->addButton(IX_TEXT("Ähnliche Bilder"), QDialogButtonBox::ActionRole);
    auto *journal = buttons->addButton(IX_TEXT("Dateijournal / Papierkorb"), QDialogButtonBox::ActionRole);
    layout->addWidget(buttons);
    PreviewService previews;
    int generation = 0;
    QSet<QString> requested;
    QHash<QString, QListWidgetItem *> items;
    ImageCatalog catalog;
    auto snapshot = catalog.records();
    auto requestVisible = [&] {
        const auto viewport = list->viewport()->rect();
        if (viewport.isEmpty())
            return;
        int first = list->count(), last = -1;
        // Corners often hit gaps between icon cells. Sample inside the viewport
        // and include adjoining rows for partially visible cells.
        const int dx = std::max(1, list->gridSize().width() / 2),
                  dy = std::max(1, list->gridSize().height() / 2);
        for (int y = 0; y < viewport.height() + dy; y += dy)
            for (int x = 0; x < viewport.width() + dx; x += dx) {
                const auto index =
                    list->indexAt({std::min(x, viewport.width() - 1), std::min(y, viewport.height() - 1)});
                if (index.isValid()) {
                    first = std::min(first, index.row());
                    last = std::max(last, index.row());
                }
            }
        if (last < 0)
            return;
        const int columns = viewport.width() / std::max(1, list->gridSize().width()) + 2;
        first = std::max(0, first - columns);
        last = std::min(list->count() - 1, last + columns);
        for (int r = first; r <= last; ++r) {
            auto *item = list->item(r);
            if (!list->visualItemRect(item).intersects(viewport))
                continue;
            auto path = item->data(Qt::UserRole).toString();
            if (!requested.contains(path)) {
                requested.insert(path);
                previews.request(path, {128, 96}, generation);
            }
        }
    };
    QTimer visible;
    visible.setSingleShot(true);
    visible.setInterval(50);
    QObject::connect(&visible, &QTimer::timeout, &dialog, requestVisible);
    auto *viewportObserver = new ViewportObserver(list->viewport(), [&] { visible.start(); });
    auto disconnectViewport = qScopeGuard([viewportObserver] { delete viewportObserver; });
    auto reload = [&] {
        previews.cancel();
        ++generation;
        items.clear();
        requested.clear();
        list->clear();
        QStringList matches;
        const auto terms = search->text().simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const auto &path : files) {
            const auto data = snapshot.value(QFileInfo(path).absoluteFilePath()).toObject();
            if (data.value(QStringLiteral("rating")).toInt() < minimum->value() ||
                (color->currentIndex() &&
                 data.value(QStringLiteral("color")).toString() != colors[color->currentIndex()]))
                continue;
            const auto haystack =
                path + QLatin1Char(' ') + data.value(QStringLiteral("metadataText")).toString();
            bool match = true;
            for (const auto &term : terms)
                if (!haystack.contains(term, Qt::CaseInsensitive)) {
                    match = false;
                    break;
                }
            if (match)
                matches.append(path);
        }
        for (const auto &path : matches) {
            auto data = snapshot.value(QFileInfo(path).absoluteFilePath()).toObject();
            QStringList annotations;
            const int stars = std::clamp(data.value(QStringLiteral("rating")).toInt(), 0, 5);
            if (stars)
                annotations.append(QString(stars, QChar(0x2605)));
            const auto colorIndex = colors.indexOf(data.value(QStringLiteral("color")).toString());
            if (colorIndex > 0)
                annotations.append(labels.at(colorIndex));
            auto label = QFileInfo(path).fileName();
            if (!annotations.isEmpty())
                label += QLatin1Char('\n') + annotations.join(QStringLiteral(" · "));
            auto *item = new QListWidgetItem(label, list);
            item->setSizeHint({160, 132});
            item->setTextAlignment(Qt::AlignHCenter);
            item->setIcon(uiIcon(QStringLiteral("image"), list->palette()));
            item->setData(Qt::UserRole, path);
            item->setToolTip(path + QLatin1Char('\n') +
                             data.value(QStringLiteral("metadataText")).toString());
            items.insert(path, item);
        }
        visible.start();
    };
    QObject::connect(&previews, &PreviewService::ready, &dialog,
                     [&](const QString &path, const QImage &image, int token) {
                         if (token == generation && items.contains(path) && !image.isNull())
                             items[path]->setIcon(QPixmap::fromImage(image));
                     });
    QObject::connect(list->verticalScrollBar(), &QScrollBar::valueChanged, &dialog, [&] { visible.start(); });
    QTimer searchDelay;
    searchDelay.setSingleShot(true);
    searchDelay.setInterval(180);
    QObject::connect(&searchDelay, &QTimer::timeout, &dialog, reload);
    QObject::connect(search, &QLineEdit::textChanged, &dialog, [&] { searchDelay.start(); });
    QObject::connect(minimum, &QSpinBox::valueChanged, &dialog, [&] { reload(); });
    QObject::connect(color, &QComboBox::currentIndexChanged, &dialog, [&] { reload(); });
    QObject::connect(list, &QListWidget::itemDoubleClicked, &dialog, [&](QListWidgetItem *item) {
        auto path = item->data(Qt::UserRole).toString();
        dialog.accept();
        open(path);
    });
    QObject::connect(list, &QListWidget::currentItemChanged, &dialog, [&](QListWidgetItem *item) {
        if (!item)
            return;
        auto data =
            snapshot.value(QFileInfo(item->data(Qt::UserRole).toString()).absoluteFilePath()).toObject();
        rating->setValue(data.value(QStringLiteral("rating")).toInt());
        mark->setCurrentIndex(
            std::max(0, int(colors.indexOf(data.value(QStringLiteral("color")).toString()))));
    });
    QObject::connect(save, &QPushButton::clicked, &dialog, [&] {
        QStringList paths;
        for (auto *item : list->selectedItems())
            paths.append(item->data(Qt::UserRole).toString());
        const auto error = catalog.setAnnotations(paths, rating->value(), colors[mark->currentIndex()]);
        if (!error.isEmpty())
            QMessageBox::warning(&dialog, IX_TEXT("Bildkatalog"), localized(error));
        snapshot = catalog.records();
        reload();
    });
    QObject::connect(journal, &QPushButton::clicked, &dialog, [&] { journalDialog(&dialog, journalRoot); });
    auto run = [&](int kind) {
        QProgressDialog progress(IX_TEXT("Bilder werden untersucht …"), IX_TEXT("Abbrechen"), 0, files.size(),
                                 &dialog);
        progress.setWindowModality(Qt::WindowModal);
        progress.setAutoClose(false);
        progress.setAutoReset(false);
        std::atomic_bool cancel = false;
        bool cancelledByUser = false;
        QObject::connect(&progress, &QProgressDialog::canceled, &progress, [&] {
            cancelledByUser = true;
            cancel = true;
        });
        struct Result {
            QString error;
            DuplicateResult duplicates;
        };
        QFutureWatcher<Result> watcher;
        QObject::connect(&watcher, &QFutureWatcherBase::finished, &progress, &QDialog::accept);
        watcher.setFuture(QtConcurrent::run([&] {
            Result result;
            auto report = [&](int value, int) {
                QMetaObject::invokeMethod(
                    &progress, [&progress, value] { progress.setValue(value); }, Qt::QueuedConnection);
            };
            if (kind == 0)
                result.error = catalog.index(files, cancel, report);
            else
                result.duplicates = ImageCatalog::duplicates(files, kind == 2, 6, cancel, report);
            return result;
        }));
        progress.exec();
        const bool completed = watcher.isFinished() && !cancelledByUser && !progress.wasCanceled();
        cancel.store(true);
        watcher.waitForFinished();
        auto result = watcher.result();
        if (!result.error.isEmpty())
            QMessageBox::warning(&dialog, IX_TEXT("Bildkatalog"), result.error);
        if (kind == 0) {
            snapshot = catalog.records();
            reload();
            status->setText(completed && result.error.isEmpty() ? IX_TEXT("Metadatenindex aktualisiert.")
                                                               : QString());
            status->setVisible(!status->text().isEmpty());
            return;
        }
        QDialog matches(&dialog);
        matches.setObjectName(QStringLiteral("duplicateDialog"));
        matches.setWindowTitle(kind == 1 ? IX_TEXT("Identische Dateien (SHA-256)")
                                         : IX_TEXT("Ähnliche Bilder – Prüfkandidaten"));
        matches.resize(900, 500);
        auto *box = new QVBoxLayout(&matches);
        auto *table = new QTableWidget(result.duplicates.pairs.size(), 3);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setHorizontalHeaderLabels({IX_TEXT("Bild A"), IX_TEXT("Bild B"), IX_TEXT("Abstand")});
        int row = 0;
        for (const auto &pair : result.duplicates.pairs) {
            table->setItem(row, 0, new QTableWidgetItem(pair.first));
            table->setItem(row, 1, new QTableWidgetItem(pair.second));
            table->setItem(row++, 2, new QTableWidgetItem(QString::number(pair.distance)));
        }
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
        box->addWidget(table);
        box->addWidget(new QLabel(
            IX_TEXT("%1 Paare; %2 nicht lesbar.%3")
                .arg(row)
                .arg(result.duplicates.errors.size())
                .arg(result.duplicates.truncated ? IX_TEXT(" Auf 10.000 Paare begrenzt.") : QString())));
        auto openImage = [&](int r, int c) {
            if (r >= 0 && c >= 0 && c < 2) {
                const auto path = table->item(r, c)->text();
                matches.accept();
                dialog.accept();
                open(path);
            }
        };
        QObject::connect(table, &QTableWidget::cellDoubleClicked, &matches, openImage);
        auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
        auto *openButton = close->addButton(IX_TEXT("Öffnen"), QDialogButtonBox::ActionRole);
        openButton->setObjectName(QStringLiteral("openDuplicateImage"));
        openButton->setEnabled(false);
        QObject::connect(table, &QTableWidget::currentCellChanged, &matches,
                         [openButton](int row, int column) {
            openButton->setEnabled(row >= 0 && column >= 0 && column < 2);
        });
        QObject::connect(openButton, &QPushButton::clicked, &matches,
                         [&] { openImage(table->currentRow(), table->currentColumn()); });
        box->addWidget(close);
        QObject::connect(close, &QDialogButtonBox::rejected, &matches, &QDialog::reject);
        matches.exec();
    };
    QObject::connect(index, &QPushButton::clicked, &dialog, [&] { run(0); });
    QObject::connect(exact, &QPushButton::clicked, &dialog, [&] { run(1); });
    QObject::connect(near, &QPushButton::clicked, &dialog, [&] { run(2); });
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    reload();
    dialog.exec();
    visible.stop();
    previews.cancel();
}
} // namespace ix
