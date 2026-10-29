// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "image_view.h"
#include <array>
namespace ix {
enum class FitWindowResize { None, Image, Screen };
struct ViewFitOption {
    const char *command;
    ImageView::Fit fit;
    FitWindowResize resize = FitWindowResize::None;
};
inline constexpr std::array<ViewFitOption, 12> viewFitOptions{{
    {"fitOriginal", ImageView::Fit::Original, FitWindowResize::Image},
    {"fitWindow", ImageView::Fit::Window},
    {"fitLarge", ImageView::Fit::LargeOnly},
    {"fitWidth", ImageView::Fit::Width},
    {"fitHeight", ImageView::Fit::Height},
    {"fitShort", ImageView::Fit::ShortSide},
    {"fitScreen", ImageView::Fit::Window, FitWindowResize::Screen},
    {"fitScreenLarge", ImageView::Fit::LargeOnly, FitWindowResize::Screen},
    {"fitScreenWidth", ImageView::Fit::Width, FitWindowResize::Screen},
    {"fitScreenHeight", ImageView::Fit::Height, FitWindowResize::Screen},
    {"fitScreenShort", ImageView::Fit::ShortSide, FitWindowResize::Screen},
    {"noFit", ImageView::Fit::Original}
}};
inline const ViewFitOption *viewFitOption(const QString &command) {
    for (const auto &option : viewFitOptions)
        if (command == QLatin1StringView(option.command)) return &option;
    return nullptr;
}
inline QString selectedViewFitCommand(ImageView::Fit fit, const QString &hint) {
    if (const auto *option = viewFitOption(hint); option && option->fit == fit) return hint;
    // Older profiles store only the enum. Pick one unambiguous menu entry;
    // LargeOnly uses the requested screen-fit default.
    switch (fit) {
    case ImageView::Fit::Window: return QStringLiteral("fitWindow");
    case ImageView::Fit::LargeOnly: return QStringLiteral("fitScreenLarge");
    case ImageView::Fit::Width: return QStringLiteral("fitWidth");
    case ImageView::Fit::Height: return QStringLiteral("fitHeight");
    case ImageView::Fit::ShortSide: return QStringLiteral("fitShort");
    default: return QStringLiteral("noFit");
    }
}
} // namespace ix
