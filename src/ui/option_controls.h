// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QCheckBox>
#include <QPainter>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QTextLayout>
#include <algorithm>
namespace ix {
inline int optionControlHeight(const QFontMetrics &metrics) {
    return std::max(32, metrics.height() + 12);
}
// Native checkbox semantics/painting, with a full-row hit target and wrapping
// that follows the actual field width rather than changing the stored label.
class OptionCheckBox final : public QCheckBox {
  public:
    explicit OptionCheckBox(const QString &label, QWidget *parent = nullptr) : QCheckBox(label, parent) {
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    QSize sizeHint() const override {
        auto size = QCheckBox::sizeHint();
        size.setHeight(optionControlHeight(fontMetrics()));
        return size;
    }
    QSize minimumSizeHint() const override { return {80, optionControlHeight(fontMetrics())}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        const auto lines = wrapped(width).count(QLatin1Char('\n')) + 1;
        return std::max(optionControlHeight(fontMetrics()), int(lines) * fontMetrics().lineSpacing() + 10);
    }
  protected:
    bool hitButton(const QPoint &point) const override { return rect().contains(point); }
    void paintEvent(QPaintEvent *) override {
        QStyleOptionButton option;
        initStyleOption(&option);
        option.text = wrapped(width());
        QPainter painter(this);
        style()->drawControl(QStyle::CE_CheckBox, &option, &painter, this);
    }
  private:
    const QString &wrapped(int width) const {
        const int padding = style()->pixelMetric(QStyle::PM_IndicatorWidth, nullptr, this) +
                            style()->pixelMetric(QStyle::PM_CheckBoxLabelSpacing, nullptr, this) + 8;
        width = std::max(32, width - padding);
        if (width == cachedWidth_ && text() == cachedSource_ && font() == cachedFont_) return cachedText_;
        cachedWidth_ = width;
        cachedSource_ = text();
        cachedFont_ = font();
        QTextLayout layout(cachedSource_, cachedFont_);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        QStringList lines;
        layout.beginLayout();
        for (;;) {
            auto line = layout.createLine();
            if (!line.isValid()) break;
            line.setLineWidth(width);
            lines.append(cachedSource_.mid(line.textStart(), line.textLength()).trimmed());
        }
        layout.endLayout();
        cachedText_ = lines.join(QLatin1Char('\n'));
        return cachedText_;
    }
    mutable int cachedWidth_ = -1;
    mutable QString cachedSource_, cachedText_;
    mutable QFont cachedFont_;
};
class OptionItemDelegate final : public QStyledItemDelegate {
  public:
    explicit OptionItemDelegate(QObject *parent) : QStyledItemDelegate(parent) {}
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        auto size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(std::max(size.height(), optionControlHeight(option.fontMetrics)));
        return size;
    }
};
} // namespace ix
