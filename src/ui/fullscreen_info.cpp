// SPDX-License-Identifier: GPL-3.0-or-later
#include "fullscreen_info.h"
#include "core/localization.h"
#include <QtWidgets>
namespace ix {
namespace {
QList<QPair<QString, QString>> fields() {
    return {{QStringLiteral("filename"), IX_TEXT("Dateiname")},
            {QStringLiteral("resolution"), IX_TEXT("Auflösung")},
            {QStringLiteral("dpi"), QStringLiteral("DPI")},
            {QStringLiteral("fileSize"), IX_TEXT("Dateigröße")},
            {QStringLiteral("format"), IX_TEXT("Dateiformat")},
            {QStringLiteral("zoom"), IX_TEXT("Zoom")},
            {QStringLiteral("camera"), IX_TEXT("Kamera")},
            {QStringLiteral("lens"), IX_TEXT("Objektiv")},
            {QStringLiteral("taken"), IX_TEXT("Aufnahmedatum")},
            {QStringLiteral("exposure"), IX_TEXT("Belichtungszeit")},
            {QStringLiteral("aperture"), IX_TEXT("Blende")},
            {QStringLiteral("iso"), QStringLiteral("ISO")},
            {QStringLiteral("focalLength"), IX_TEXT("Brennweite")},
            {QStringLiteral("copyright"), IX_TEXT("Copyright")}};
}
}
FullscreenInfoOptions fullscreenInfoOptions() {
    const QSettings settings;
    FullscreenInfoOptions options;
    options.enabled = settings.value(QStringLiteral("fullscreen/info/enabled"), false).toBool();
    const int corner = settings.value(QStringLiteral("fullscreen/info/corner"), int(Qt::TopLeftCorner)).toInt();
    if (corner >= int(Qt::TopLeftCorner) && corner <= int(Qt::BottomRightCorner))
        options.corner = Qt::Corner(corner);
    if (settings.contains(QStringLiteral("fullscreen/info/fields")))
        options.fields = settings.value(QStringLiteral("fullscreen/info/fields")).toStringList();
    return options;
}
QString fullscreenInfoText(const ImageDocument &document, const FullscreenInfoOptions &options, double zoom) {
    if (!options.enabled || document.empty())
        return {};
    const auto tag = [&](std::initializer_list<const char *> keys) -> QString {
        if (document.metadata)
            for (const auto *key : keys) {
                auto text = document.metadata->tags.value(QString::fromLatin1(key)).simplified();
                if (!text.isEmpty())
                    return text;
            }
        return {};
    };
    QStringList lines;
    for (const auto &[key, label] : fields()) {
        if (!options.fields.contains(key))
            continue;
        QString value;
        if (key == QStringLiteral("filename"))
            value = document.path.isEmpty() ? IX_TEXT("Zwischenablage") : QFileInfo(document.path).fileName();
        else if (key == QStringLiteral("resolution"))
            value = QStringLiteral("%1 × %2 px").arg(document.pixels.width()).arg(document.pixels.height());
        else if (key == QStringLiteral("dpi")) {
            if (document.pixels.dotsPerMeterX() > 0 && document.pixels.dotsPerMeterY() > 0)
                value = QStringLiteral("%1 × %2").arg(document.dpiX(), 0, 'f', 1).arg(document.dpiY(), 0, 'f', 1);
        } else if (key == QStringLiteral("fileSize")) {
            if (!document.path.isEmpty() && document.fileBytes >= 0)
                value = uiLocale().formattedDataSize(document.fileBytes);
        } else if (key == QStringLiteral("format"))
            value = QString::fromLatin1(document.format).toUpper();
        else if (key == QStringLiteral("zoom"))
            value = QStringLiteral("%1 %").arg(zoom * 100, 0, 'f', 1);
        else if (key == QStringLiteral("camera"))
            value = tag({"Exif.Image.Model", "Xmp.tiff.Model"});
        else if (key == QStringLiteral("lens"))
            value = tag({"Exif.Photo.LensModel", "Xmp.exifEX.LensModel"});
        else if (key == QStringLiteral("taken"))
            value = tag({"Exif.Photo.DateTimeOriginal", "Xmp.exif.DateTimeOriginal", "Exif.Image.DateTime"});
        else if (key == QStringLiteral("exposure"))
            value = tag({"Exif.Photo.ExposureTime", "Xmp.exif.ExposureTime"});
        else if (key == QStringLiteral("aperture"))
            value = tag({"Exif.Photo.FNumber", "Xmp.exif.FNumber"});
        else if (key == QStringLiteral("iso"))
            value = tag({"Exif.Photo.PhotographicSensitivity", "Exif.Photo.ISOSpeedRatings", "Xmp.exif.ISOSpeedRatings"});
        else if (key == QStringLiteral("focalLength"))
            value = tag({"Exif.Photo.FocalLength", "Xmp.exif.FocalLength"});
        else if (key == QStringLiteral("copyright"))
            value = tag({"Exif.Image.Copyright", "Xmp.dc.rights", "Iptc.Application2.Copyright"});
        if (!value.isEmpty())
            lines.append(label + QStringLiteral(": ") + value.simplified().left(512));
    }
    return lines.join(QLatin1Char('\n'));
}
FullscreenInfoEditor::FullscreenInfoEditor(QWidget *parent) : QWidget(parent) {
    const auto initial = fullscreenInfoOptions();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    enabled_ = new QCheckBox(IX_TEXT("Informationen im Vollbild anzeigen"));
    enabled_->setObjectName(QStringLiteral("fullscreen/info/enabled"));
    enabled_->setChecked(initial.enabled);
    layout->addWidget(enabled_);
    auto *details = new QWidget;
    auto *form = new QFormLayout(details);
    form->setContentsMargins(0, 0, 0, 0);
    corner_ = new QComboBox;
    corner_->setObjectName(QStringLiteral("fullscreen/info/corner"));
    corner_->addItem(IX_TEXT("Oben links"), int(Qt::TopLeftCorner));
    corner_->addItem(IX_TEXT("Oben rechts"), int(Qt::TopRightCorner));
    corner_->addItem(IX_TEXT("Unten links"), int(Qt::BottomLeftCorner));
    corner_->addItem(IX_TEXT("Unten rechts"), int(Qt::BottomRightCorner));
    corner_->setCurrentIndex(std::max(0, corner_->findData(int(initial.corner))));
    form->addRow(IX_TEXT("Position:"), corner_);
    auto *choices = new QWidget;
    auto *grid = new QGridLayout(choices);
    grid->setContentsMargins(0, 0, 0, 0);
    int index = 0;
    for (const auto &[key, label] : fields()) {
        auto *box = new QCheckBox(label);
        box->setObjectName(QStringLiteral("fullscreen/info/field/") + key);
        box->setChecked(initial.fields.contains(key));
        fields_.insert(key, box);
        grid->addWidget(box, index / 2, index % 2);
        ++index;
    }
    form->addRow(choices);
    layout->addWidget(details);
    connect(enabled_, &QCheckBox::toggled, details, &QWidget::setEnabled);
    details->setEnabled(initial.enabled);

}
FullscreenInfoOptions FullscreenInfoEditor::options() const {
    FullscreenInfoOptions result;
    result.enabled = enabled_->isChecked();
    result.corner = Qt::Corner(corner_->currentData().toInt());
    result.fields.clear();
    for (auto it = fields_.cbegin(); it != fields_.cend(); ++it)
        if (it.value()->isChecked())
            result.fields.append(it.key());
    return result;
}
void FullscreenInfoEditor::store(QVariantMap &values) const {
    const auto selected = options();
    values.insert(QStringLiteral("fullscreen/info/enabled"), selected.enabled);
    values.insert(QStringLiteral("fullscreen/info/corner"), int(selected.corner));
    values.insert(QStringLiteral("fullscreen/info/fields"), selected.fields);
}
bool fullscreenInfoDialog(QWidget *parent) {
    QDialog dialog(parent);
    dialog.setObjectName(QStringLiteral("fullscreenInfoDialog"));
    dialog.setWindowTitle(IX_TEXT("Informationen anzeigen (Vollbildmodus)"));
    auto *layout = new QVBoxLayout(&dialog);
    auto *editor = new FullscreenInfoEditor;
    layout->addWidget(editor);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&dialog, editor] {
        QSettings settings;
        QVariantMap values;
        editor->store(values);
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            settings.setValue(it.key(), it.value());
        settings.sync();
        if (settings.status() == QSettings::NoError)
            dialog.accept();
        else
            QMessageBox::warning(&dialog, IX_TEXT("Einstellungen"), IX_TEXT("Einstellungen konnten nicht gespeichert werden."));
    });
    return dialog.exec() == QDialog::Accepted;
}
} // namespace ix
