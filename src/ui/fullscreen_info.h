// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "core/image_document.h"
#include <QWidget>
#include <QMap>
#include <QVariantMap>
class QCheckBox;
class QComboBox;
namespace ix {
struct FullscreenInfoOptions {
    bool enabled = false;
    Qt::Corner corner = Qt::TopLeftCorner;
    QStringList fields{QStringLiteral("resolution"), QStringLiteral("dpi"), QStringLiteral("fileSize")};
};
FullscreenInfoOptions fullscreenInfoOptions();
QString fullscreenInfoText(const ImageDocument &, const FullscreenInfoOptions &, double zoom);
class FullscreenInfoEditor : public QWidget {
  public:
    explicit FullscreenInfoEditor(QWidget *parent = nullptr);
    FullscreenInfoOptions options() const;
    void store(QVariantMap &values) const;
  private:
    QCheckBox *enabled_;
    QComboBox *corner_;
    QMap<QString, QCheckBox *> fields_;
};
bool fullscreenInfoDialog(QWidget *parent);
} // namespace ix
