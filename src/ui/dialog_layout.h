// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFormLayout>
#include <QScrollArea>
#include <QVBoxLayout>
#include "core/localization.h"
namespace ix {
inline QVBoxLayout *dialogLayout(QWidget *parent) {
    parent->setLocale(uiLocale());
    auto *layout = new QVBoxLayout(parent);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    return layout;
}
inline QFormLayout *dialogFormLayout() {
    auto *layout = new QFormLayout;
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(8);
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
