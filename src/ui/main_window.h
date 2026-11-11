// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#pragma once
#include "core/codec_registry.h"
#include "core/batch_processor.h"
#include "core/document_history.h"
#include "document_activity.h"
#include "folder_traversal.h"
#include "image_view.h"
#include "fullscreen_info.h"
#include "slideshow_controller.h"
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QJsonObject>
#include <QMainWindow>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <optional>
#include <atomic>
#include <memory>
class QToolBar;
class QComboBox;
class QLabel;
class QAction;
class QActionGroup;
class QMenu;
namespace ix {
class PreviewStrip;
class ActionSearch;
class DirectoryIndexService;
class SaveOptionsDialog;
struct WorkResult {
    ImageDocument document;
    QImage display;
    QString error;
    std::optional<QRect> selection = std::nullopt;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QWidget *parent = nullptr, QString cacheRoot = {});
    ~MainWindow() override;
    void openPath(QString path, int page = 0, bool checkChanges = true);
    const ImageDocument &document() const {
        return document_;
    }
    bool isBusy() const {
        return activity_.busy();
    }
    QStringList implementedCommands() const;
    void dispatch(const QString &command);
    void applyControlSettings();

  protected:
    bool event(QEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void changeEvent(QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

  private:
    QString cacheRoot_;
    CodecRegistry registry_;
    // Persistent Save As companion + background warmup state (see
    // saveCompanion()/scheduleSaveUiWarmup()): building the dialog and scanning
    // codec capabilities are the two costs that made Save As feel slow.
    SaveOptionsDialog *saveOptionsDialog_ = nullptr;
    QString saveOptionsLanguage_;
    QFuture<void> saveFormatsWarmup_;
    bool saveUiWarmupScheduled_ = false;
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
    bool indexedAllFiles_ = false;
    UnreadableFiles unreadableFiles_ = UnreadableFiles::Skip;
    void buildExtraFeatures();
    void updatePreviews();
    void reconcileFilePath(const BatchFileChanges &changes = {});
    // True while the view shows the black state of a file this window removed: the
    // path stays for the listing and the status bar, but nothing can be read from it.
    bool isRemovalPlaceholder() const;
    QComboBox *zoom_;
    QList<QLabel *> status_;
    QMap<QString, QAction *> actions_;
    QActionGroup *viewFitActions_ = nullptr;
    QString windowFitCommand_;
    QJsonObject menuData_;
    QStringList files_;
    int fileIndex_ = -1;
    DocumentHistory history_;
    QRect lastSelection_;
    bool invertedSelection_ = false;
    QFutureWatcher<WorkResult> worker_;
    // Speculative decode of the neighbour in the navigation direction. One
    // low-priority worker; results land in the bounded ImageLoadCache and are
    // only installed when the user actually navigates there.
    QThreadPool prefetchPool_;
    std::shared_ptr<std::atomic_bool> prefetchCancel_;
    int prefetchStep_ = 1;
    void schedulePrefetch();
    enum class WorkMode { Load, Edit, Page, Display, Selection, Undo, Redo };
    WorkMode workMode_ = WorkMode::Load;
    DocumentActivity activity_;
    bool lockZoom_ = false, lockScroll_ = false, displaySharpen_ = false;
    bool zoomEdited_ = false;
    bool smoothZoom_ = true, smoothFit_ = true;
    int pendingNavigation_ = 0;
    // A deletion can leave the view black on purpose; these keep the removed
    // document's path and the two neighbours so the first navigation resumes from
    // the slot it held instead of from the edge of the listing.
    QString removedDocumentPath_, resumeNextPath_, resumePreviousPath_;
    qsizetype removedFileIndex_ = -1;
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
    void toggleZoomAt(QPointF viewportPosition);
    bool zoomToggled_ = false;
    ImageView::Fit fitBeforeToggle_ = ImageView::Fit::LargeOnly;
    double zoomBeforeToggle_ = 1.0;
    int scrollBeforeToggleX_ = 0, scrollBeforeToggleY_ = 0;
    SlideshowController slideshow_;
    QTimer animationTimer_, deferredOpenTimer_;
    struct DeferredOpen {
        QString path;
        int page;
        bool checkChanges;
        std::optional<FolderTraversal> navigation = std::nullopt;
    };
    std::optional<DeferredOpen> deferredOpen_;
    std::optional<DeferredOpen> activeOpen_;
    void openRequested(DeferredOpen request);
    void handleLoadFailure(const QString &error, DeferredOpen request);
    void openFolderPath(QString path, bool wrap, bool checkChanges = true);
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
    void syncFitActions();
    void updateStatus();
    // The status message area names the running transaction, so a blocked
    // window never looks idle and rejected input explains itself.
    QString busyHint() const;
    void syncBusyHint();
    void showBusyHint(bool rejected);
    QTimer busyHintTimer_;
    QTimer busyHintDelay_;
    // Text of our own status overlay, so we never clear or overwrite an
    // unrelated message (and never overlay the image info while saving —
    // the modal progress dialog already reports that phase).
    QString pendingBusyText_;
    void refreshFiles(bool force = false);
    void navigate(int direction);
    void page(int direction);
    bool confirmDiscard();
    bool saveImage(bool selection = false, bool forceDialog = false);
    // One persistent companion dialog + background format warmup: Save As must
    // not rebuild ~20 widgets or rescan codec capabilities on every open.
    SaveOptionsDialog &saveCompanion();
    void scheduleSaveUiWarmup();
    void fileOperation(const QString &operation);
    void operation(std::function<QImage(const QImage &)> fn, bool streaming = false);
    void effect(std::function<QImage(const QImage &)> fn);
    void installDocument(ImageDocument document, QImage display = {}, bool clearHistory = true);
    void beginWork(std::function<WorkResult()> fn, WorkMode mode);
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
    void deliverDeferredOpen();
    std::shared_ptr<std::atomic_bool> loadCancelled_;
};
} // namespace ix
