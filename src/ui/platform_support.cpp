// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "platform_support.h"
#include "core/localization.h"
#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <QFontDatabase>
#include <QImage>
#include <QLayout>
#include <QPointer>
#include <QScreen>
#include <QStyleFactory>
#include <QTimer>
#include <QVariant>
#include <QWidget>
#include <QWindow>
#include <bit>
#include <algorithm>
#include <cstring>
#include <utility>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#include <dwmapi.h>
#endif
namespace ix {
namespace {
#ifdef Q_OS_WIN
// A QWidget shown immediately before its owner is resized may not have received
// its first expose/paint yet. DwmFlush cannot flush pixels Qt has not submitted.
// Upload an opaque native surface before showing it; no Qt event pumping or
// reentrant user commands are needed to make the snapshot ready.
class SnapshotCover final : public QObject {
  public:
    explicit SnapshotCover(QWidget *owner) : QObject(owner) {
        setObjectName(QStringLiteral("fullscreenTransitionCover"));
    }
    ~SnapshotCover() override {
        if (handle_)
            DestroyWindow(handle_);
    }
    HWND handle() const { return handle_; }
    bool prepare(HWND owner, QImage snapshot) {
        RECT rect{};
        if (snapshot.isNull() || !GetWindowRect(owner, &rect))
            return false;
        const QSize size(rect.right - rect.left, rect.bottom - rect.top);
        if (size.isEmpty())
            return false;
        // GetWindowRect and the native surface use physical pixels, including
        // fractional DPI and monitors whose desktop coordinates are negative.
        if (snapshot.size() != size)
            snapshot = snapshot.scaled(size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        snapshot = snapshot.convertToFormat(QImage::Format_RGB32);
        if (snapshot.isNull())
            return false;
        static const ATOM windowClass = [] {
            WNDCLASSW wc{};
            wc.lpfnWndProc = DefWindowProcW;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.lpszClassName = L"uxzViewSnapshotCover";
            return RegisterClassW(&wc);
        }();
        if (!windowClass)
            return false;
        handle_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                     WS_EX_TRANSPARENT | WS_EX_TOPMOST,
                                 MAKEINTATOM(windowClass), L"", WS_POPUP,
                                 rect.left, rect.top, size.width(), size.height(), owner,
                                 nullptr, GetModuleHandleW(nullptr), nullptr);
        if (!handle_)
            return false;
        HDC dc = CreateCompatibleDC(nullptr);
        if (!dc)
            return false;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = size.width();
        info.bmiHeader.biHeight = -size.height(); // top-down, like QImage
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void *pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        bool uploaded = false;
        if (bitmap && pixels) {
            const auto previous = SelectObject(dc, bitmap);
            if (previous && previous != HGDI_ERROR) {
                const size_t stride = size_t(size.width()) * 4;
                for (int y = 0; y < size.height(); ++y)
                    std::memcpy(static_cast<unsigned char *>(pixels) + size_t(y) * stride,
                                snapshot.constScanLine(y), stride);
                POINT destination{rect.left, rect.top}, source{};
                SIZE nativeSize{size.width(), size.height()};
                // UpdateLayeredWindow owns the submitted content after return.
                // ULW_OPAQUE avoids any alpha/colour-key holes in the cover.
                uploaded = UpdateLayeredWindow(handle_, nullptr, &destination, &nativeSize,
                                               dc, &source, 0, nullptr, ULW_OPAQUE);
                SelectObject(dc, previous);
            }
        }
        if (bitmap)
            DeleteObject(bitmap);
        DeleteDC(dc);
        if (uploaded)
            setProperty("nativeHandle", QVariant::fromValue(quintptr(handle_)));
        return uploaded;
    }
  private:
    HWND handle_ = nullptr;
};

class CaptureHotkey final : public QObject, public QAbstractNativeEventFilter {
  public:
    CaptureHotkey(QWidget *owner, int id, std::function<void()> capture)
        : QObject(owner), id_(id), capture_(std::move(capture)) {
        setObjectName(QStringLiteral("captureGlobalShortcut"));
        qApp->installNativeEventFilter(this);
    }
    ~CaptureHotkey() override {
        UnregisterHotKey(nullptr, id_);
        qApp->removeNativeEventFilter(this);
    }
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override {
        const auto *event = static_cast<MSG *>(message);
        if (event->message != WM_HOTKEY || event->wParam != WPARAM(id_))
            return false;
        QTimer::singleShot(0, this, capture_);
        return true;
    }

  private:
    int id_;
    std::function<void()> capture_;
};
#endif
} // namespace
struct WindowTransition::NativeState
#ifdef Q_OS_WIN
    : QObject, QAbstractNativeEventFilter {
    using Set = HRESULT (WINAPI *)(HWND, DWORD, LPCVOID, DWORD);
    using Flush = HRESULT (WINAPI *)();
    HWND handle;
    Set set = nullptr;
    Flush flush = nullptr;
    BOOL previous = FALSE;
    bool disabled = false, filtering = true;
    std::unique_ptr<SnapshotCover> cover;
    QPointer<QWidget> owner;
    QList<QRect> previousGeometry;
    int frames = 0;
    bool painted = false, presented = false, completed = false;
    const bool tracing = qEnvironmentVariableIsSet("IX_PROFILE_TRANSITIONS");
    QElapsedTimer elapsed;
    double snapshotMs = 0, coverReadyMs = 0, geometryRestoredMs = 0, restoreMs = 0;
    int presentations = 0;
    bool exiting = false;

    explicit NativeState(QWidget *window) : QObject(window), handle(reinterpret_cast<HWND>(window->winId())), owner(window) {
        if (tracing) elapsed.start();
        exiting = window->isFullScreen();
        static const HMODULE module = LoadLibraryExW(L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Get = HRESULT (WINAPI *)(HWND, DWORD, PVOID, DWORD);
        static const auto get = module ? std::bit_cast<Get>(GetProcAddress(module, "DwmGetWindowAttribute")) : nullptr;
        set = module ? std::bit_cast<Set>(GetProcAddress(module, "DwmSetWindowAttribute")) : nullptr;
        flush = module ? std::bit_cast<Flush>(GetProcAddress(module, "DwmFlush")) : nullptr;
        const BOOL forceDisabled = TRUE;
        // This per-window setting is owned by IxView. Some DWM versions only
        // support setting it, so the normal state is the fallback on read failure.
        if (get)
            get(handle, DWMWA_TRANSITIONS_FORCEDISABLED, &previous, sizeof(previous));
        disabled = set && SUCCEEDED(set(handle, DWMWA_TRANSITIONS_FORCEDISABLED,
                                       &forceDisabled, sizeof(forceDisabled)));
        if (window->isFullScreen()) {
            // Native style/geometry changes can still expose a partially restored
            // frame through DWM. Keep the last complete fullscreen frame above
            // the owner until its client, previews and frame have been painted.
            const auto snapshot = window->grab().toImage();
            if (tracing) snapshotMs = elapsed.nsecsElapsed() / 1e6;
            if (!snapshot.isNull()) {
                cover = std::make_unique<SnapshotCover>(window);
                if (!cover->prepare(handle, snapshot))
                    cover.reset();
                if (cover && set) {
                    const auto coverHandle = cover->handle();
                    set(coverHandle, DWMWA_TRANSITIONS_FORCEDISABLED, &forceDisabled, sizeof(forceDisabled));
                    // Windows 11 can otherwise round this temporary cover's
                    // corners, exposing pixels below it. Older DWM ignores it.
                    // Documented values; retain compatibility with older SDK
                    // headers which do not declare the Windows 11 enum yet.
                    constexpr DWORD cornerPreferenceAttribute = 33;
                    constexpr DWORD noRounding = 1; // DWMWCP_DONOTROUND
                    set(coverHandle, cornerPreferenceAttribute, &noRounding, sizeof(noRounding));
                }
                if (cover) {
                    SetWindowPos(cover->handle(), HWND_TOPMOST, 0, 0, 0, 0,
                                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
                    // The pixels are now submitted, unlike a newly shown Qt
                    // widget with a pending first expose. Fence presentation
                    // once before changing the owner's native size.
                    // https://learn.microsoft.com/windows/win32/api/dwmapi/nf-dwmapi-dwmflush
                    GdiFlush();
                    const bool ready = flush && SUCCEEDED(flush());
                    cover->setProperty("compositorPresented", ready);
                }
            }
        }
        if (tracing) coverReadyMs = elapsed.nsecsElapsed() / 1e6;
        qApp->installNativeEventFilter(this);
    }
    bool nativeEventFilter(const QByteArray &, void *message, qintptr *result) override {
        auto *event = static_cast<MSG *>(message);
        if (event->hwnd != handle)
            return false;
        // Qt changes the native style/geometry before the widget layout is ready.
        // Keep those intermediate client/frame updates off screen, without
        // WM_SETREDRAW (which changes WS_VISIBLE and affects Qt's state restore).
        if (event->message == WM_WINDOWPOSCHANGING)
            reinterpret_cast<WINDOWPOS *>(event->lParam)->flags |= SWP_NOREDRAW;
        else if (event->message == WM_NCPAINT || event->message == WM_ERASEBKGND) {
            *result = event->message == WM_ERASEBKGND ? 1 : 0;
            return true;
        }
        return false;
    }
    void resume() {
        if (std::exchange(filtering, false))
            qApp->removeNativeEventFilter(this);
    }
    void finishAfterPresentation() {
        if (tracing) restoreMs = elapsed.nsecsElapsed() / 1e6;
        setObjectName(QStringLiteral("pendingFullscreenTransition"));
        // Re-enabling QWidget updates posts layout/backing-store work. A
        // synchronous DwmFlush cannot submit those pending Qt events for us.
        QTimer::singleShot(0, this, [this] { settle(); });
    }
    void settle() {
        ++presentations;
        if (!owner || !owner->isVisible() || owner->isMinimized() || !IsWindow(handle)) {
            deleteLater();
            return;
        }
        if (owner->layout())
            owner->layout()->activate();
        const auto children = owner->findChildren<QWidget *>();
        for (auto *child : children)
            if (child->window() == owner && child->isVisible() && child->layout())
                child->layout()->activate();
        QList<QRect> geometry{owner->frameGeometry()};
        for (auto *child : children)
            if (child->window() == owner && child->isVisible())
                geometry.append(child->geometry());
        const bool changed = !painted || geometry != previousGeometry;
        const bool exposed = !owner->windowHandle() || owner->windowHandle()->isExposed();
        if (changed) {
            previousGeometry = geometry;
            // Render only after layout changes, not once per display interval.
            owner->repaint();
            RedrawWindow(handle, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
            painted = true;
            presented = false;
        } else if (!presented && exposed) {
            // Restore the owner's DWM setting while it is still covered. Doing
            // this after destroying the cover exposes late decoration updates.
            restoreTransitions();
            // One compositor fence for the completed frame. Another queued turn
            // checks for layout work generated by the final paint/presentation.
            if (flush) flush();
            presented = true;
        } else if (presented) {
            completed = true;
            deleteLater();
            return;
        }
        if (++frames >= 12) {
            restoreTransitions();
            if (!presented && flush) flush();
            completed = true;
            deleteLater();
            return;
        }
        // Normally continue on the next event-loop turn. Only an unexposed
        // native window needs to wait for the display, not every restored frame.
        const auto rate = owner->screen() ? owner->screen()->refreshRate() : 60.0;
        const int interval = exposed ? 0 : std::clamp(qRound(1000.0 / std::max(1.0, rate)), 8, 33);
        QTimer::singleShot(interval, this, [this] { settle(); });
    }
    void restoreTransitions() {
        if (disabled && owner && IsWindow(handle)) {
            set(handle, DWMWA_TRANSITIONS_FORCEDISABLED, &previous, sizeof(previous));
            disabled = false;
        }
    }
    ~NativeState() override {
        resume();
        restoreTransitions();
        if (!completed && owner && IsWindow(handle)) {
            RedrawWindow(handle, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN | RDW_FRAME);
            if (flush)
                flush();
        }
        cover.reset();
        if (tracing) qInfo().noquote() << QJsonDocument(QJsonObject{
            {QStringLiteral("fullscreenTransition"), exiting ? QStringLiteral("exit") : QStringLiteral("enter")},
            {QStringLiteral("snapshotMs"), snapshotMs}, {QStringLiteral("coverReadyMs"), coverReadyMs},
            {QStringLiteral("geometryRestoredMs"), geometryRestoredMs}, {QStringLiteral("restoreMs"), restoreMs}, {QStringLiteral("totalMs"), elapsed.nsecsElapsed() / 1e6},
            {QStringLiteral("presentationPasses"), presentations}}).toJson(QJsonDocument::Compact);
    }
#else
    {
    void resume() {}
#endif
};
WindowTransition::WindowTransition(QWidget *window) : window_(window), updates_(window->updatesEnabled()) {
#ifdef Q_OS_WIN
    // A rapid second toggle ends the previous cover and restores its DWM state
    // before capturing a new transition; no timers from that cover survive.
    delete window->findChild<QObject *>(QStringLiteral("pendingFullscreenTransition"), Qt::FindDirectChildrenOnly);
    if (updates_ && window->isVisible() && QGuiApplication::platformName() == QStringLiteral("windows"))
        native_ = std::make_unique<NativeState>(window);
#endif
    window_->setUpdatesEnabled(false);
}
WindowTransition::~WindowTransition() {
#ifdef Q_OS_WIN
    if (native_ && native_->tracing)
        native_->geometryRestoredMs = native_->elapsed.nsecsElapsed() / 1e6;
#endif
    if (native_)
        native_->resume();
    window_->setUpdatesEnabled(updates_);
#ifdef Q_OS_WIN
    if (native_ && native_->cover) {
        // The queued presentation path paints the restored layout once. A
        // synchronous paint here would render it again before queued resizes.
        native_->finishAfterPresentation();
        native_.release(); // QObject parent/timers now own the pending transition.
        return;
    }
#endif
    if (updates_)
        window_->repaint();
}

QString configureGlobalCaptureShortcut(QWidget *owner, const QKeySequence &sequence,
                                       std::function<void()> capture) {
    delete owner->findChild<QObject *>(QStringLiteral("captureGlobalShortcut"), Qt::FindDirectChildrenOnly);
    if (sequence.isEmpty())
        return {};
#ifdef Q_OS_WIN
    const auto combination = sequence[0];
    const auto key = combination.key();
    UINT vk = 0;
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24)
        vk = VK_F1 + key - Qt::Key_F1;
    else if ((key >= Qt::Key_A && key <= Qt::Key_Z) || (key >= Qt::Key_0 && key <= Qt::Key_9))
        vk = UINT(key);
    else if (key == Qt::Key_Print)
        vk = VK_SNAPSHOT;
    if (!vk)
        return IX_TEXT("Dieses globale Tastenkürzel wird nicht unterstützt.");
    UINT modifiers = MOD_NOREPEAT;
    const auto qt = combination.keyboardModifiers();
    if (qt.testFlag(Qt::ControlModifier))
        modifiers |= MOD_CONTROL;
    if (qt.testFlag(Qt::AltModifier))
        modifiers |= MOD_ALT;
    if (qt.testFlag(Qt::ShiftModifier))
        modifiers |= MOD_SHIFT;
    if (qt.testFlag(Qt::MetaModifier))
        modifiers |= MOD_WIN;
    static int nextId = 0x4958;
    const int id = nextId++;
    if (!RegisterHotKey(nullptr, id, modifiers, vk))
        return IX_TEXT("Das globale Tastenkürzel ist bereits belegt oder kann nicht registriert werden.");
    new CaptureHotkey(owner, id, std::move(capture));
    return {};
#else
    Q_UNUSED(capture);
    return IX_TEXT("Globale Screenshot-Tastenkürzel werden derzeit nur unter Windows unterstützt.");
#endif
}
QFont referenceFont(int points) {
    const bool korean = uiLanguage() == QStringLiteral("ko");
    const bool japanese = uiLanguage() == QStringLiteral("ja");
    QFont font = QFontDatabase::systemFont(QFontDatabase::GeneralFont);
#ifdef Q_OS_WIN
    if (korean)
        font.setFamilies({QStringLiteral("Malgun Gothic"), QStringLiteral("Segoe UI")});
    else if (japanese)
        font.setFamilies({QStringLiteral("Yu Gothic UI"), QStringLiteral("Meiryo UI")});
    else
        font.setFamily(QStringLiteral("MS Shell Dlg 2"));
#else
    if (korean)
        font.setFamilies({QStringLiteral("Noto Sans CJK KR"), QStringLiteral("Noto Sans KR"), font.family()});
    else if (japanese)
        font.setFamilies({QStringLiteral("Noto Sans CJK JP"), QStringLiteral("Noto Sans JP"), font.family()});
#endif
    // Tiny legacy dialog fonts render Hangul/Kana poorly through fallback.
    font.setPointSize(korean || japanese ? std::max(points, 9) : points);
    if (korean || japanese) {
        font.setStyleName({});
        font.setStyleHint(QFont::SansSerif, QFont::PreferAntialias);
    }
    return font;
}
void configureUiFont() {
    auto font = referenceFont(9);
#ifdef Q_OS_WIN
    if (uiLanguage() != QStringLiteral("ko") && uiLanguage() != QStringLiteral("ja"))
        font.setFamily(QStringLiteral("Segoe UI"));
#endif
    QApplication::setFont(font);
}
void configureDesktopAppearance() {
    auto *style = QStyleFactory::create(QStringLiteral("Fusion"));
    if (!style)
        style = QStyleFactory::create(QStringLiteral("windows"));
    if (style)
        QApplication::setStyle(style);
    configureUiFont();
}
} // namespace ix
