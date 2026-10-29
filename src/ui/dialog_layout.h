// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFormLayout>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QLabel>
#include "core/localization.h"
namespace ix {
namespace spacing {
inline constexpr int dialogMargin = 16;
inline constexpr int section = 12;
inline constexpr int column = 16;
inline constexpr int row = 8;
}
inline QLabel *dialogFieldLabel(const QString &text, QWidget *field) {
    auto *label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setBuddy(field);
    return label;
}
inline QVBoxLayout *dialogLayout(QWidget *parent) {
    parent->setLocale(uiLocale());
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(spacing::dialogMargin, spacing::dialogMargin,
                               spacing::dialogMargin, spacing::dialogMargin);
    layout->setSpacing(spacing::section);
    return layout;
}
inline QFormLayout *dialogFormLayout() {
    auto *layout = new QFormLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(spacing::column);
    layout->setVerticalSpacing(spacing::row);
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
    return layout;
}
// Keep action buttons outside the scrolling content on small/high-DPI screens.
inline QWidget *dialogBody(QVBoxLayout *outer) {
    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    auto *body = new QWidget;
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);
    return body;
}
} // namespace ix
