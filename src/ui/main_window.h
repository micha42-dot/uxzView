// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "core/codec_registry.h"
#include "document_activity.h"
#include "image_view.h"
#include "fullscreen_info.h"
#include "slideshow_controller.h"
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QMainWindow>
#include <QTimer>
#include <functional>
#include <optional>
class QToolBar;
class QComboBox;
class QLabel;
class QAction;
class QMenu;
namespace ix {
class PreviewStrip;
class ActionSearch;
class DirectoryIndexService;
struct WorkResult {
    ImageDocument document;
    QImage display;
    QString error;
    std::optional<QRect> selection = std::nullopt;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr, QString journalRoot = {});
    void openPath(const QString &path, int page = 0, bool checkChanges = true);
    const ImageDocument &document() const {
        return document_;
    }
    bool isBusy() const {
        return activity_.busy();
    }
    QStringList implementedCommands() const;
    void dispatch(const QString &command);

  protected:
    bool eventFilter(QObject *, QEvent *) override;
    void changeEvent(QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    QString journalRoot_;
    CodecRegistry registry_;
    ImageDocument document_;
    ImageView *view_;
    QToolBar *toolbar_ = nullptr;
    PreviewStrip *previewStrip_ = nullptr;
    ActionSearch *actionSearch_ = nullptr;
    QAction *searchWidgetAction_ = nullptr;
    QAction *searchSpacerAction_ = nullptr;
    DirectoryIndexService *directoryIndex_;
    QFileSystemWatcher directoryWatcher_;
    QTimer directoryDebounce_;
    QHash<QString, int> filePositions_;
    QString indexedDirectory_;
    int indexedSort_ = -1;
    bool indexedReverse_ = false, indexReady_ = false;
    void buildExtraFeatures();
    void updatePreviews();
    void reconcileFilePath();
    QComboBox *zoom_;
    QList<QLabel *> status_;
    QMap<QString, QAction *> actions_;
    QJsonObject menuData_;
    QStringList files_;
    int fileIndex_ = -1;
    struct HistoryEntry : ImageDocument {
        QRect selection;
        HistoryEntry(ImageDocument document, QRect selected)
            : ImageDocument(std::move(document)), selection(selected) {}
    };
    QVector<HistoryEntry> undo_, redo_;
    QRect lastSelection_;
    bool invertedSelection_ = false;
    QFutureWatcher<WorkResult> worker_;
    enum class WorkMode { Load, Edit, Page, Display, Selection, Undo, Redo };
    WorkMode workMode_ = WorkMode::Load;
    DocumentActivity activity_;
    bool lockZoom_ = false, lockScroll_ = false, displaySharpen_ = false;
    bool zoomEdited_ = false;
    int pendingNavigation_ = 0;
    int loadGeneration_ = 0;
    bool rawPreviewShown_ = false;
    ImageView::Fit windowBrowsingFit_ = ImageView::Fit::LargeOnly;
    QByteArray windowGeometry_;
    bool fullscreenScreenChanged_ = false;
    bool windowWasMaximized_ = false;
    bool slideshowFullscreen_ = false;
    FullscreenInfoOptions fullscreenInfo_;

    void configureCaptureShortcut();
    void advanceSlideshow();
    SlideshowController slideshow_;
    QTimer animationTimer_, deferredOpenTimer_;
    struct DeferredOpen {
        QString path;
        int page;
        bool checkChanges;
    };
    std::optional<DeferredOpen> deferredOpen_;
    int animationLoops_ = 0;
    bool animationPlaying_ = false, animationEnded_ = false;
    QFileSystemWatcher hotfolder_;
    QString watchedFolder_;
    void buildMenus();
    void populateMenu(QMenu *, const QJsonObject &);
    void populateCropMenu(QMenu *);
    void buildToolbar();
    void showKeyboardHelp();
    void refreshState();
    void updateStatus();
    void refreshFiles(bool force = false);
    void navigate(int direction);
    void page(int direction);
    bool confirmDiscard(bool changingImage = false);
    bool saveImage(bool selection = false, bool forceDialog = false);
    void fileOperation(const QString &operation);
    void operation(std::function<QImage(const QImage &)> fn);
    void effect(std::function<QImage(const QImage &)> fn);
    void installDocument(ImageDocument document, QImage display = {}, bool clearHistory = true);
    void beginWork(std::function<WorkResult()> fn, WorkMode mode);
    void pushUndo();
    void restoreHistory(bool redo);
    void applySettings(bool applyFit = false);
    void applyWindowAppearance(bool fullscreen);
    void refreshToolbarIcons();
    void fullscreen();
    void commitZoomEdit();
    void startSlideshow(bool showDialog);
    void updateRecent(const QString &key, const QString &path);
    void recentMenu(QMenu *, const QString &key);
    void showError(const QString &error);
    void stopAnimation();
    void displayCurrent();
};
} // namespace ix
