// SPDX-License-Identifier: GPL-3.0-or-later
#include "pipeline_step_editor.h"
#include "dialog_layout.h"
#include "resize_filters.h"
#include "core/codec_registry.h"
#include "core/localization.h"
#include <QtWidgets>
#include <algorithm>
namespace ix {
QString pipelineStepTitle(const PipelineStep &s) {
    auto p = s.parameters;
    if (s.type == QStringLiteral("resize"))
        return IX_TEXT("Größe ändern: %1 × %2")
            .arg(p.value(QStringLiteral("width")).toInt())
            .arg(p.value(QStringLiteral("height")).toInt());
    if (s.type == QStringLiteral("sharpen"))
        return IX_TEXT("Schärfen: %1").arg(p.value(QStringLiteral("amount")).toInt(20));
    if (s.type == QStringLiteral("watermark"))
        return IX_TEXT("Wasserzeichen: ") +
               p.value(QStringLiteral("text"))
                   .toString(QFileInfo(p.value(QStringLiteral("image")).toString()).fileName());
    return IX_TEXT("Als %1 speichern (Qualität %2)")
        .arg(p.value(QStringLiteral("format")).toString().toUpper())
        .arg(p.value(QStringLiteral("quality")).toInt(95));
}
bool editPipelineStep(PipelineStep &step, QWidget *parent) {
    QDialog d(parent);
    d.setWindowTitle(IX_TEXT("Verarbeitungsschritt"));
    auto *v = dialogLayout(&d);
    auto *form = dialogFormLayout();
    v->addLayout(form);
    QJsonObject p = step.parameters;
    auto spin = [&](QString label, int value, int low, int high) {
        auto *s = new QSpinBox;
        s->setRange(low, high);
        s->setValue(value);
        form->addRow(label, s);
        return s;
    };
    std::function<QJsonObject()> values;
    if (step.type == QStringLiteral("resize")) {
        auto *w = spin(IX_TEXT("Maximale Breite:"), p.value(QStringLiteral("width")).toInt(2048), 1, 100000),
             *h = spin(IX_TEXT("Maximale Höhe:"), p.value(QStringLiteral("height")).toInt(2048), 1, 100000);
        auto *proportional = new QCheckBox(IX_TEXT("Seitenverhältnis erhalten"));
        proportional->setChecked(p.value(QStringLiteral("proportional")).toBool(true));
        form->addRow(proportional);
        auto *filter = new QComboBox;
        filter->addItems(resizeFilterLabels());
        filter->setCurrentIndex(std::clamp(p.value(QStringLiteral("filter")).toInt(5), 0, filter->count() - 1));
        form->addRow(IX_TEXT("Filter:"), filter);
        values = [=] {
            return QJsonObject{{QStringLiteral("width"), w->value()},
                               {QStringLiteral("height"), h->value()},
                               {QStringLiteral("proportional"), proportional->isChecked()},
                               {QStringLiteral("filter"), filter->currentIndex()}};
        };
    } else if (step.type == QStringLiteral("sharpen")) {
        auto *amount = spin(IX_TEXT("Stärke:"), p.value(QStringLiteral("amount")).toInt(20), 1, 99);
        values = [=] { return QJsonObject{{QStringLiteral("amount"), amount->value()}}; };
    } else if (step.type == QStringLiteral("watermark")) {
        auto *text = new QLineEdit(p.value(QStringLiteral("text")).toString(IX_TEXT("Wasserzeichen")));
        form->addRow(IX_TEXT("Text:"), text);
        auto *image = new QLineEdit(p.value(QStringLiteral("image")).toString());
        auto *choose = new QPushButton(IX_TEXT("Bild statt Text..."));
        form->addRow(choose, image);
        QObject::connect(choose, &QPushButton::clicked, &d, [=, &d] {
            auto path = QFileDialog::getOpenFileName(&d, IX_TEXT("Wasserzeichen-Bild"), {},
                                                     CodecRegistry().openFilter());
            if (!path.isEmpty())
                image->setText(path);
        });
        auto *size = spin(IX_TEXT("Schriftgröße (Pixel):"), p.value(QStringLiteral("fontSize")).toInt(32), 1,
                          1000),
             *margin = spin(IX_TEXT("Rand (Pixel):"), p.value(QStringLiteral("margin")).toInt(20), 0, 10000);
        auto *opacity = new QDoubleSpinBox;
        opacity->setRange(0, 1);
        opacity->setSingleStep(.05);
        opacity->setValue(p.value(QStringLiteral("opacity")).toDouble(.65));
        form->addRow(IX_TEXT("Deckkraft:"), opacity);
        auto *color = new QLineEdit(p.value(QStringLiteral("color")).toString(QStringLiteral("#ffffff")));
        form->addRow(IX_TEXT("Textfarbe (#RRGGBB):"), color);
        auto *anchor = new QComboBox;
        anchor->addItem(IX_TEXT("Rechts unten"), QStringLiteral("bottomRight"));
        anchor->addItem(IX_TEXT("Mitte"), QStringLiteral("center"));
        anchor->addItem(IX_TEXT("Links oben"), QStringLiteral("topLeft"));
        anchor->setCurrentIndex(std::max(0, anchor->findData(p.value(QStringLiteral("anchor")).toString())));
        form->addRow(IX_TEXT("Position:"), anchor);
        values = [=] {
            return QJsonObject{{QStringLiteral("text"), text->text()},
                               {QStringLiteral("image"), image->text()},
                               {QStringLiteral("fontSize"), size->value()},
                               {QStringLiteral("margin"), margin->value()},
                               {QStringLiteral("opacity"), opacity->value()},
                               {QStringLiteral("color"), color->text()},
                               {QStringLiteral("anchor"), anchor->currentData().toString()}};
        };
    } else {
        auto *format = new QComboBox;
        for (const auto &cap : CodecRegistry().capabilities())
            if (cap.write)
                format->addItem(cap.name, cap.extensions.first());
        format->setCurrentIndex(std::max(
            0, format->findData(p.value(QStringLiteral("format")).toString(QStringLiteral("webp")))));
        form->addRow(IX_TEXT("Format:"), format);
        auto *quality = spin(IX_TEXT("Qualität:"), p.value(QStringLiteral("quality")).toInt(95), 0, 100);
        values = [=] {
            return QJsonObject{{QStringLiteral("format"), format->currentData().toString()},
                               {QStringLiteral("quality"), quality->value()}};
        };
    }
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    v->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    if (d.exec() != QDialog::Accepted)
        return false;
    step.parameters = values();
    return true;
}
} // namespace ix
