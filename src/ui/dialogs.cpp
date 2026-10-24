// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "dialogs.h"
#include "core/batch_processor.h"
#include "core/localization.h"
#include "platform_support.h"
#include "settings_dialog.h"
#include <QColorSpace>
#include <QImageReader>
#include <QtConcurrent>
#include <QtWidgets>
#include <atomic>
namespace ix {
namespace {
void buttons(QDialog &d, QVBoxLayout *layout) {
    auto *b = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    b->button(QDialogButtonBox::Cancel)->setText(IX_TEXT("Abbrechen"));
    QObject::connect(b, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(b, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    layout->addWidget(b);
}
QSpinBox *integer(QFormLayout *f, QString label, int value, int low, int high) {
    auto *box = new QSpinBox;
    box->setRange(low, high);
    box->setValue(value);
    f->addRow(label, box);
    return box;
}
} // namespace
ColorDialog::ColorDialog(const QImage &image, QWidget *parent) : QDialog(parent) {
    setWindowTitle(IX_TEXT("Farben ändern"));
    setFixedSize(490, 420);
    auto label = [&](QString text, QRect rect) {
        auto *w = new QLabel(text, this);
        w->setGeometry(rect);
        return w;
    };
    label(IX_TEXT("Originalbild"), {10, 2, 225, 18})->setAlignment(Qt::AlignCenter);
    label(IX_TEXT("Neues Bild"), {250, 2, 232, 18})->setAlignment(Qt::AlignCenter);
    originalLabel_ = label({}, {8, 22, 230, 150});
    originalLabel_->setAlignment(Qt::AlignCenter);
    originalLabel_->setFrameStyle(QFrame::Box);
    originalLabel_->installEventFilter(this);
    preview_ = label({}, {250, 22, 232, 150});
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFrameStyle(QFrame::Box);
    auto group = [&](QString name, QRect rect) {
        auto *g = new QGroupBox(name, this);
        g->setGeometry(rect);
        return g;
    };
    group(IX_TEXT("Helligkeit:"), {8, 174, 230, 44});
    group(IX_TEXT("Kontrast:"), {250, 174, 232, 44});
    group(IX_TEXT("Farb-Balancierung:"), {8, 220, 230, 96});
    group(IX_TEXT("Gamma-Korrektur:"), {250, 220, 232, 44});
    group(IX_TEXT("Farbsättigung:"), {250, 267, 232, 49});
    for (int i = 0; i < 7; ++i) {
        int x = i == 0 || i >= 4 ? 16 : 260;
        int y = i == 0 || i == 1 ? 189 : i == 2 ? 236 : i == 3 ? 283 : 237 + (i - 4) * 24;
        int span = i >= 4 ? 155 : 175;
        if (i >= 4) {
            label(QStringList{QStringLiteral("R:"), QStringLiteral("G:"), QStringLiteral("B:")}[i - 4],
                  {16, y, 20, 20});
            x = 36;
        }
        auto *slider = new QSlider(Qt::Horizontal, this);
        slider->setGeometry(x, y, span, 20);
        auto *spin = new QDoubleSpinBox(this);
        spin->setLocale(QLocale::c());
        spin->setGeometry(i == 0 || i >= 4 ? 205 : 441, y, 35, 21);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setRange(i == 2             ? .01
                       : i == 1 || i == 3 ? -100
                                          : -255,
                       i == 2             ? 10
                       : i == 1 || i == 3 ? 100
                                          : 255);
        spin->setDecimals(i == 2 ? 2 : 0);
        spin->setValue(i == 2 ? 1 : 0);
        inputs_.append(spin);
        slider->setRange(i == 2 ? 1 : int(spin->minimum()), i == 2 ? 1000 : int(spin->maximum()));
        slider->setValue(i == 2 ? 100 : 0);
        connect(slider, &QSlider::valueChanged, this,
                [spin, i](int v) { spin->setValue(i == 2 ? v / 100.0 : v); });
        connect(spin, &QDoubleSpinBox::valueChanged, this, [this, slider, i](double v) {
            QSignalBlocker block(slider);
            slider->setValue(qRound(i == 2 ? v * 100 : v));
            updatePreview();
        });
    }
    auto reset = [this] {
        for (int i = 0; i < inputs_.size(); ++i)
            inputs_[i]->setValue(i == 2 ? 1 : 0);
    };
    group(IX_TEXT("Profile:"), {8, 318, 230, 67});
    auto *profiles = new QComboBox(this);
    profiles->setEditable(true);
    profiles->setGeometry(18, 334, 212, 21);
    setImage(image);
    QSettings settings;
    settings.beginGroup(QStringLiteral("color/profiles"));
    profiles->addItems(settings.childKeys());
    settings.endGroup();
    auto button = [&](QString title, QRect rect, auto fn) {
        auto *b = new QPushButton(title, this);
        b->setGeometry(rect);
        connect(b, &QPushButton::clicked, this, fn);
        return b;
    };
    button(IX_TEXT("Laden"), {18, 359, 61, 22}, [=, this] {
        auto list = QSettings().value(QStringLiteral("color/profiles/") + profiles->currentText()).toList();
        if (list.size() == 7)
            for (int i = 0; i < 7; ++i)
                inputs_[i]->setValue(list[i].toDouble());
    });
    button(IX_TEXT("Löschen"), {89, 359, 61, 22}, [=] {
        QSettings().remove(QStringLiteral("color/profiles/") + profiles->currentText());
        profiles->removeItem(profiles->currentIndex());
    });
    button(IX_TEXT("Speichern"), {162, 359, 68, 22}, [=, this] {
        QString name = profiles->currentText().trimmed();
        if (name.isEmpty() || name.contains(QLatin1Char('/')))
            return;
        QVariantList list;
        for (auto *spin : inputs_)
            list << spin->value();
        QSettings().setValue(QStringLiteral("color/profiles/") + name, list);
        if (profiles->findText(name) < 0)
            profiles->addItem(name);
    });
    auto *apply =
        button(IX_TEXT("Auf Bild anwenden"), {259, 331, 106, 23}, [this] { emit applyRequested(values()); });
    button(IX_TEXT("Standardwerte"), {377, 331, 105, 23}, reset);
    auto *remember = new QCheckBox(IX_TEXT("Werte speichern"), this);
    remember->setGeometry(258, 360, 200, 20);
    remember->setChecked(settings.value(QStringLiteral("color/remember"), false).toBool());
    if (remember->isChecked()) {
        auto values = settings.value(QStringLiteral("color/values")).toList();
        if (values.size() == 7)
            for (int i = 0; i < 7; ++i)
                inputs_[i]->setValue(values[i].toDouble());
    }
    label(
        IX_TEXT("Info: Klicken Sie ins Originalbild (heller Bereich)\num Weißabgleich zu ändern (RGB-Werte)"),
        {14, 385, 229, 32})
        ->setFont(referenceFont(8));
    auto *ok = button(QStringLiteral("OK"), {259, 390, 106, 23}, [this, remember] {
        QSettings s;
        s.setValue(QStringLiteral("color/remember"), remember->isChecked());
        QVariantList values;
        for (auto *spin : inputs_)
            values << spin->value();
        s.setValue(QStringLiteral("color/values"), values);
        accept();
    });
    ok->setDefault(true);
    button(IX_TEXT("Abbrechen"), {377, 390, 105, 23}, [this] { reject(); });
    Q_UNUSED(apply);
    updatePreview();
}
void ColorDialog::setImage(const QImage &image) {
    original_ = image.scaled(228, 148, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    originalLabel_->setPixmap(QPixmap::fromImage(original_));
    for (int i = 0; i < inputs_.size(); ++i)
        inputs_[i]->setValue(i == 2 ? 1 : 0);
    updatePreview();
}
bool ColorDialog::eventFilter(QObject *object, QEvent *event) {
    if (object == originalLabel_ && event->type() == QEvent::MouseButtonPress && !original_.isNull()) {
        auto *mouse = static_cast<QMouseEvent *>(event);
        QPoint offset((originalLabel_->width() - original_.width()) / 2,
                      (originalLabel_->height() - original_.height()) / 2);
        QPoint pixel = mouse->position().toPoint() - offset;
        if (original_.rect().contains(pixel)) {
            auto c = original_.pixelColor(pixel);
            int maximum = std::max({c.red(), c.green(), c.blue()});
            inputs_[4]->setValue(maximum - c.red());
            inputs_[5]->setValue(maximum - c.green());
            inputs_[6]->setValue(maximum - c.blue());
        }
        return true;
    }
    return QDialog::eventFilter(object, event);
}
ColorAdjust ColorDialog::values() const {
    ColorAdjust o;
    o.brightness = inputs_[0]->value();
    o.contrast = inputs_[1]->value();
    o.gamma = inputs_[2]->value();
    o.saturation = inputs_[3]->value();
    o.red = int(inputs_[4]->value());
    o.green = int(inputs_[5]->value());
    o.blue = int(inputs_[6]->value());
    return o;
}
void ColorDialog::updatePreview() {
    preview_->setPixmap(QPixmap::fromImage(adjustColors(original_, values())));
}
bool newImageDialog(QWidget *p, ImageDocument &out) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("Neues Bild erzeugen"));
    auto *v = new QVBoxLayout(&d);
    auto *f = new QFormLayout;
    v->addLayout(f);
    auto *w = integer(f, IX_TEXT("Bildbreite:"), 100, 1, 100000),
         *h = integer(f, IX_TEXT("Bildhöhe:"), 100, 1, 100000),
         *dx = integer(f, QStringLiteral("X-DPI:"), 96, 1, 100000),
         *dy = integer(f, QStringLiteral("Y-DPI:"), 96, 1, 100000);
    auto *alpha = new QCheckBox(IX_TEXT("Hintergrund ist transparent"));
    v->addWidget(alpha);
    auto *gray = new QCheckBox(IX_TEXT("Graustufen erstellen"));
    v->addWidget(gray);
    QColor color(255, 128, 0);
    auto *choose = new QPushButton(IX_TEXT("Hintergrundfarbe"));
    v->addWidget(choose);
    QObject::connect(choose, &QPushButton::clicked, &d, [&] {
        auto c = QColorDialog::getColor(color, &d);
        if (c.isValid())
            color = c;
    });
    buttons(d, v);
    if (d.exec() != QDialog::Accepted)
        return false;
    if (!safeSize({w->value(), h->value()})) {
        QMessageBox::warning(p, IX_TEXT("Neues Bild"), IX_TEXT("Bildgröße überschreitet das Speicherlimit."));
        return false;
    }
    out = {};
    out.pixels =
        QImage(w->value(), h->value(), alpha->isChecked() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
    out.pixels.fill(alpha->isChecked() ? QColor(Qt::transparent) : color);
    out.pixels.setDotsPerMeterX(qRound(dx->value() / .0254));
    out.pixels.setDotsPerMeterY(qRound(dy->value() / .0254));
    out.pixels.setColorSpace(QColorSpace::SRgb);
    if (gray->isChecked())
        out.pixels = grayscale(out.pixels);
    out.modified = true;
    return !out.empty();
}
bool selectionDialog(QWidget *p, QSize image, QRect &r) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("Spezielle Markierung erstellen"));
    auto *v = new QVBoxLayout(&d);
    auto *f = new QFormLayout;
    v->addLayout(f);
    auto *x = integer(f, IX_TEXT("X-Position:"), r.x(), 0, image.width() - 1),
         *y = integer(f, IX_TEXT("Y-Position:"), r.y(), 0, image.height() - 1),
         *w = integer(f, IX_TEXT("Breite:"), r.isEmpty() ? image.width() : r.width(), 1, image.width()),
         *h = integer(f, IX_TEXT("Höhe:"), r.isEmpty() ? image.height() : r.height(), 1, image.height());
    buttons(d, v);
    if (d.exec() != QDialog::Accepted)
        return false;
    r = QRect(x->value(), y->value(), w->value(), h->value()).intersected(QRect(QPoint(), image));
    return true;
}
bool canvasDialog(QWidget *p, int &l, int &r, int &t, int &b, QColor &c) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("Farb-Rahmen hinzufügen (Leinwandgröße ändern)"));
    auto *v = new QVBoxLayout(&d);
    auto *f = new QFormLayout;
    v->addLayout(f);
    auto *ls = integer(f, IX_TEXT("Links:"), l, -100000, 100000),
         *rs = integer(f, IX_TEXT("Rechts:"), r, -100000, 100000),
         *ts = integer(f, IX_TEXT("Oben:"), t, -100000, 100000),
         *bs = integer(f, IX_TEXT("Unten:"), b, -100000, 100000);
    auto *choose = new QPushButton(IX_TEXT("Rahmenfarbe"));
    v->addWidget(choose);
    QObject::connect(choose, &QPushButton::clicked, &d, [&] {
        auto n = QColorDialog::getColor(c, &d);
        if (n.isValid())
            c = n;
    });
    buttons(d, v);
    if (d.exec() != QDialog::Accepted)
        return false;
    l = ls->value();
    r = rs->value();
    t = ts->value();
    b = bs->value();
    return true;
}
void imageInformation(QWidget *p, const ImageDocument &d) {
    QDialog dialog(p);
    dialog.setWindowTitle(IX_TEXT("Bild-Information"));
    dialog.resize(570, 440);
    auto *v = new QVBoxLayout(&dialog);
    auto *f = new QFormLayout;
    v->addLayout(f);
    auto row = [&](QString a, QString b) {
        auto *l = new QLabel(b);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        f->addRow(a, l);
    };
    row(IX_TEXT("Dateiname:"), d.path.isEmpty() ? IX_TEXT("Zwischenablage") : d.path);
    row(IX_TEXT("Bildformat:"), QString::fromLatin1(d.format).toUpper());
    row(IX_TEXT("Aktuelle Größe:"),
        QStringLiteral("%1 x %2 Pixel").arg(d.pixels.width()).arg(d.pixels.height()));
    row(IX_TEXT("Farbtiefe:"), QStringLiteral("%1 BPP").arg(d.pixels.depth()));
    row(IX_TEXT("Auflösung:"),
        QStringLiteral("%1 x %2 DPI").arg(d.dpiX(), 0, 'f', 2).arg(d.dpiY(), 0, 'f', 2));
    row(IX_TEXT("Speicherbedarf:"),
        QStringLiteral("%1 MiB").arg(d.pixels.sizeInBytes() / 1048576.0, 0, 'f', 2));
    row(IX_TEXT("Seite:"), QStringLiteral("%1 / %2").arg(d.page + 1).arg(d.pageCount));
    row(IX_TEXT("Farbprofil:"), d.pixels.colorSpace().isValid() ? d.pixels.colorSpace().description()
                                                                : IX_TEXT("Kein eingebettetes ICC-Profil"));
    row(IX_TEXT("Alphakanal:"), d.pixels.hasAlphaChannel() ? IX_TEXT("Ja") : IX_TEXT("Nein"));
    auto *text = new QPlainTextEdit;
    text->setReadOnly(true);
    QString report;
    for (auto it = d.text.begin(); it != d.text.end(); ++it)
        report += it.key() + QStringLiteral(": ") + it.value() + QLatin1Char('\n');
    if (d.metadata)
        for (auto it = d.metadata->tags.begin(); it != d.metadata->tags.end(); ++it)
            report += it.key() + QStringLiteral(": ") + it.value() + QLatin1Char('\n');
    if (!d.metadataWarning.isEmpty())
        report += IX_TEXT("Metadaten-Hinweis: ") + d.metadataWarning;
    text->setPlainText(report);
    v->addWidget(text);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    v->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    dialog.exec();
}
void histogramDialog(QWidget *parent, const QImage &image) {
    QDialog dialog(parent);
    dialog.setWindowTitle(IX_TEXT("Histogramm"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *chart = new QLabel(IX_TEXT("Histogramm wird berechnet …"));
    chart->setObjectName(QStringLiteral("histogramChart"));
    chart->setFixedSize(768, 280);
    chart->setAlignment(Qt::AlignCenter);
    layout->addWidget(chart);
    layout->addWidget(new QLabel(QStringLiteral(
        "0                                      RGB · Häufigkeit                                      255")));
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    QFutureWatcher<QImage> watcher;
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &dialog, [&] {
        auto result = watcher.result();
        if (!result.isNull())
            chart->setPixmap(QPixmap::fromImage(result));
    });
    watcher.setFuture(QtConcurrent::run([image, cancelled] {
        QVector<quint64> r(256), g(256), b(256);
        // Convert a row at a time so the histogram does not duplicate a large source.
        for (int y = 0; y < image.height(); ++y) {
            if (cancelled->load())
                return QImage();
            auto line = image.copy(0, y, image.width(), 1).convertToFormat(QImage::Format_ARGB32);
            if (line.isNull())
                return QImage();
            auto *pixels = reinterpret_cast<const QRgb *>(line.constScanLine(0));
            for (int x = 0; x < image.width(); ++x) {
                ++r[qRed(pixels[x])];
                ++g[qGreen(pixels[x])];
                ++b[qBlue(pixels[x])];
            }
        }
        quint64 maximum = 1;
        for (int i = 0; i < 256; ++i)
            maximum = std::max({maximum, r[i], g[i], b[i]});
        QImage plot(768, 280, QImage::Format_RGB32);
        plot.fill(Qt::black);
        QPainter painter(&plot);
        for (int c = 0; c < 3; ++c) {
            painter.setPen(QPen(c == 0 ? Qt::red : c == 1 ? Qt::green : Qt::blue, 1));
            auto &data = c == 0 ? r : c == 1 ? g : b;
            for (int i = 0; i < 255; ++i)
                painter.drawLine(i * 3, 279 - int(data[i] * 270 / maximum), (i + 1) * 3,
                                 279 - int(data[i + 1] * 270 / maximum));
        }
        painter.end();
        return plot;
    }));
    dialog.exec();
    cancelled->store(true);
    watcher.waitForFinished();
}
void formatDialog(QWidget *p, const CodecRegistry &registry) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("üxzView – verfügbare Bildformate"));
    d.resize(650, 450);
    auto *v = new QVBoxLayout(&d);
    auto *table = new QTableWidget(0, 5);
    table->setHorizontalHeaderLabels({IX_TEXT("Format"), IX_TEXT("Erweiterungen"), IX_TEXT("Lesen"),
                                      IX_TEXT("Schreiben"), IX_TEXT("Bibliothek")});
    for (const auto &f : registry.capabilities()) {
        int row = table->rowCount();
        table->insertRow(row);
        QStringList cells{f.name, f.extensions.join(QStringLiteral(", ")),
                          f.read ? IX_TEXT("Ja") : IX_TEXT("Nein"), f.write ? IX_TEXT("Ja") : IX_TEXT("Nein"),
                          f.library};
        for (int c = 0; c < 5; ++c)
            table->setItem(row, c, new QTableWidgetItem(cells[c]));
    }
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    v->addWidget(table);
    auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
    v->addWidget(close);
    QObject::connect(close, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    d.exec();
}
void batchDialog(QWidget *p, const QStringList &initial, const QString &journalRoot) {
    QDialog d(p);
    d.setWindowTitle(IX_TEXT("Batch(Stapel)-Konvertierung/Umbenennung"));
    d.resize(720, 570);
    auto *v = new QVBoxLayout(&d);
    auto *files = new QListWidget;
    files->addItems(initial);
    files->setSelectionMode(QAbstractItemView::ExtendedSelection);
    v->addWidget(files);
    auto *add = new QPushButton(IX_TEXT("Dateien hinzufügen..."));
    v->addWidget(add);
    QObject::connect(add, &QPushButton::clicked, &d, [&] {
        auto paths =
            QFileDialog::getOpenFileNames(&d, IX_TEXT("Bilddateien"), {}, CodecRegistry().openFilter());
        for (const auto &path : paths)
            if (files->findItems(path, Qt::MatchExactly).isEmpty())
                files->addItem(path);
    });
    auto *remove = new QPushButton(IX_TEXT("Aus Liste entfernen"));
    v->addWidget(remove);
    QObject::connect(remove, &QPushButton::clicked, &d, [&] { qDeleteAll(files->selectedItems()); });
    auto *f = new QFormLayout;
    v->addLayout(f);
    auto *mode = new QComboBox;
    mode->addItems({IX_TEXT("Batch-Konvertierung"), IX_TEXT("Batch-Umbenennung (Dateien verschieben)")});
    f->addRow(IX_TEXT("Arbeite als:"), mode);
    auto *format = new QComboBox;
    for (const auto &c : CodecRegistry().capabilities())
        if (c.write)
            format->addItem(c.name, c.extensions.first());
    format->setCurrentIndex(std::max(0, format->findData(QStringLiteral("png"))));
    f->addRow(IX_TEXT("Zielformat:"), format);
    auto *pattern = new QLineEdit(QStringLiteral("{name}_####"));
    f->addRow(IX_TEXT("Name ({name}, ####):"), pattern);
    auto *output = new QLineEdit;
    auto *choose = new QPushButton(IX_TEXT("Zielverzeichnis wählen..."));
    f->addRow(choose, output);
    QObject::connect(choose, &QPushButton::clicked, &d, [&] {
        auto path = QFileDialog::getExistingDirectory(&d, IX_TEXT("Zielverzeichnis"));
        if (!path.isEmpty())
            output->setText(path);
    });
    auto *width = integer(f, IX_TEXT("Max. Breite (0 = unverändert):"), 0, 0, 100000),
         *height = integer(f, IX_TEXT("Max. Höhe (0 = unverändert):"), 0, 0, 100000);
    auto *loss =
        new QCheckBox(IX_TEXT("Verlust von EXIF/Animation/Farbtiefe/Transparenz beim Export zulassen"));
    v->addWidget(loss);
    auto *note = new QLabel(IX_TEXT(
        "Vorhandene Dateien werden übersprungen. Erfolgreiche Umbenennungen bleiben bei Abbruch bestehen."));
    note->setWordWrap(true);
    v->addWidget(note);
    auto *progress = new QProgressBar;
    v->addWidget(progress);
    auto *log = new QPlainTextEdit;
    log->setReadOnly(true);
    v->addWidget(log);
    auto *controls = new QDialogButtonBox;
    auto *start = controls->addButton(IX_TEXT("Starten"), QDialogButtonBox::ActionRole);
    auto *close = controls->addButton(IX_TEXT("Schließen"), QDialogButtonBox::RejectRole);
    v->addWidget(controls);
    auto cancellation = std::make_shared<std::atomic_bool>(false);
    QFutureWatcher<QList<BatchEntry>> watcher;
    bool running = false;
    QObject::connect(start, &QPushButton::clicked, &d, [&] {
        if (running) {
            cancellation->store(true);
            return;
        }
        if (output->text().isEmpty() || !files->count()) {
            QMessageBox::warning(&d, QStringLiteral("Batch"),
                                 IX_TEXT("Dateien und Zielverzeichnis auswählen."));
            return;
        }
        BatchOptions o;
        o.journalRoot = journalRoot;
        for (int i = 0; i < files->count(); ++i)
            o.inputs << files->item(i)->text();
        o.outputDirectory = output->text();
        o.format = format->currentData().toString().toLatin1();
        o.namePattern = pattern->text();
        o.renameOnly = mode->currentIndex() == 1;
        o.allowLoss = loss->isChecked();
        if (width->value() && height->value())
            o.resizeTo = {width->value(), height->value()};
        cancellation->store(false);
        running = true;
        start->setText(IX_TEXT("Abbrechen"));
        close->setEnabled(false);
        progress->setRange(0, 0);
        log->clear();
        watcher.setFuture(
            QtConcurrent::run([o, cancellation] { return BatchProcessor::run(o, *cancellation); }));
    });
    QObject::connect(&watcher, &QFutureWatcherBase::finished, &d, [&] {
        for (const auto &e : watcher.result())
            log->appendPlainText((e.error.isEmpty() ? QStringLiteral("OK: ") : IX_TEXT("FEHLER: ")) +
                                 e.input + QStringLiteral(" → ") + e.output +
                                 (e.error.isEmpty() ? QString() : QStringLiteral("\n  ") + e.error));
        running = false;
        progress->setRange(0, 1);
        progress->setValue(1);
        start->setText(IX_TEXT("Starten"));
        close->setEnabled(true);
    });
    QObject::connect(controls, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    QObject::connect(&d, &QDialog::finished, &d, [&] { cancellation->store(true); });
    d.exec();
    cancellation->store(true);
    watcher.waitForFinished();
}
bool settingsDialog(QWidget *p) {
    SettingsDialog dialog(p);
    return dialog.exec() == QDialog::Accepted;
}
void thumbnailsDialog(QWidget *p, const QStringList &files, const std::function<void(QString)> &open) {
    QDialog d(p);
    d.setWindowTitle(QStringLiteral("üxzView Thumbnails"));
    d.resize(850, 570);
    auto *v = new QVBoxLayout(&d);
    auto *list = new QListWidget;
    list->setViewMode(QListView::IconMode);
    list->setIconSize({128, 96});
    list->setGridSize({155, 130});
    list->setResizeMode(QListView::Adjust);
    v->addWidget(list);
    for (const auto &file : files) {
        auto *item = new QListWidgetItem(QFileInfo(file).fileName(), list);
        item->setData(Qt::UserRole, file);
        item->setToolTip(file);
    }
    auto *progress = new QLabel(IX_TEXT("Vorschaubilder werden geladen …"));
    v->addWidget(progress);
    auto *watcher = new QFutureWatcher<QList<QImage>>(&d);
    QObject::connect(watcher, &QFutureWatcherBase::finished, &d, [=] {
        auto images = watcher->result();
        for (int i = 0; i < images.size(); ++i)
            list->item(i)->setIcon(QPixmap::fromImage(images[i]));
        progress->setText(IX_TEXT("%1 Dateien").arg(files.size()));
    });
    auto cancel = std::make_shared<std::atomic_bool>(false);
    watcher->setFuture(QtConcurrent::run([files, cancel] {
        QList<QImage> all;
        for (const auto &file : files) {
            if (cancel->load())
                break;
            QImageReader reader(file);
            reader.setAutoTransform(true);
            auto size = reader.size();
            if (size.isValid())
                reader.setScaledSize(size.scaled(128, 96, Qt::KeepAspectRatio));
            auto img = reader.read();
            if (!img.isNull())
                img = img.scaled(128, 96, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            all.append(img);
        }
        return all;
    }));
    QObject::connect(list, &QListWidget::itemDoubleClicked, &d, [&](QListWidgetItem *item) {
        open(item->data(Qt::UserRole).toString());
        d.accept();
    });
    d.exec();
    cancel->store(true);
}
} // namespace ix
