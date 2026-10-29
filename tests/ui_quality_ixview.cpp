// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/localization.h"
#include "core/directory_index.h"
#include "ui/appearance.h"
#include "ui/dialogs.h"
#include "ui/extension_dialog.h"
#include "ui/interface_language.h"
#include "ui/main_window.h"
#include "ui/pipeline_dialog.h"
#include "ui/settings_dialog.h"
#include <QAccessible>
#include <QScopeGuard>
#include <QtTest>
#include <QtWidgets>
#include <cmath>

namespace {
double luminance(QColor color) {
    auto linear = [](double v) { return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4); };
    return .2126 * linear(color.redF()) + .7152 * linear(color.greenF()) + .0722 * linear(color.blueF());
}
double contrast(QColor a, QColor b) {
    const double first = luminance(a), second = luminance(b);
    return (std::max(first, second) + .05) / (std::min(first, second) + .05);
}
}
class UiQualityTests : public QObject {
    Q_OBJECT
    QTemporaryDir temporary_;
    void variants() {
        QTest::addColumn<QString>("theme"); QTest::addColumn<QString>("language");
        for (const auto *theme : {"light", "dark"})
            for (const auto *language : {"de", "en", "fr", "ko", "ja"})
                QTest::newRow((QByteArray(theme) + '-' + language).constData())
                    << QString::fromLatin1(theme) << QString::fromLatin1(language);
    }
    void probe(const std::function<void()> &open, const std::function<void(QDialog *)> &inspect) {
        bool inspected = false;
        QTimer timer;
        connect(&timer, &QTimer::timeout, this, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            timer.stop(); inspected = true;
            auto close = qScopeGuard([&] { dialog->reject(); });
            inspect(dialog);
        });
        timer.start(25); open(); timer.stop(); QVERIFY(inspected);
    }
    void capture(QWidget *widget, const QString &name) {
        const auto output = QStringLiteral(IX_UI_OUTPUT "/ui-audit");
        QVERIFY(QDir().mkpath(output));
        QVERIFY(widget->grab().save(QDir(output).filePath(name + QStringLiteral(".png"))));
    }
  private slots:
    void initTestCase() {
        QVERIFY(temporary_.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("IxViewUiQualityTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Isolated"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary_.path());
        QStandardPaths::setTestModeEnabled(true);
        ix::applyInterfaceLanguage(QStringLiteral("de"));
        ix::applyInterfaceTheme(QStringLiteral("light"));
    }
    void compactSettingsPages_data() { variants(); }
    void compactSettingsPages() {
        QFETCH(QString, theme); QFETCH(QString, language);
        ix::applyInterfaceLanguage(language); ix::applyInterfaceTheme(theme);
        ix::SettingsDialog dialog;
        dialog.show(); QTest::qWait(20);
        QVERIFY(dialog.width() <= 800); QVERIFY(dialog.height() <= 500);
        auto *categories=dialog.findChild<QListWidget *>(QStringLiteral("settingsCategories"));
        auto *pages=dialog.findChild<QStackedWidget *>(QStringLiteral("settingsPages"));
        QVERIFY(categories && pages); QCOMPARE(categories->count(),9);
        const auto size=dialog.size();
        for(int i=0;i<categories->count();++i) {
            categories->setCurrentRow(i); QCoreApplication::processEvents();
            QCOMPARE(dialog.size(),size);
            QVERIFY(categories->viewport()->rect().contains(categories->visualItemRect(categories->item(i))));
            auto *scroll=qobject_cast<QScrollArea *>(pages->currentWidget()); QVERIFY(scroll);
            QCOMPARE(scroll->horizontalScrollBar()->maximum(),0);
            for(auto *number:scroll->findChildren<QSpinBox *>()) QVERIFY(number->width() <= 200);
            for(auto *number:scroll->findChildren<QDoubleSpinBox *>()) QVERIFY(number->width() <= 200);
            if(i==0 || i==3) capture(&dialog,QStringLiteral("settings-compact-%1-%2-%3").arg(theme,language).arg(i));
        }
        auto *buttons=dialog.findChild<QDialogButtonBox *>(); QVERIFY(buttons && buttons->isVisible());
    }
    void paletteTextReadability_data() {
        QTest::addColumn<QString>("theme");
        QTest::newRow("light") << QStringLiteral("light");
        QTest::newRow("dark") << QStringLiteral("dark");
    }
    void paletteTextReadability() {
        QFETCH(QString, theme); ix::applyInterfaceTheme(theme);
        const auto palette = QApplication::palette();
        for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
            for (const auto pair : {qMakePair(QPalette::Text, QPalette::Base),
                                   qMakePair(QPalette::WindowText, QPalette::Window),
                                   qMakePair(QPalette::ButtonText, QPalette::Button),
                                   qMakePair(QPalette::HighlightedText, QPalette::Highlight)}) {
                const auto ratio = contrast(palette.color(group, pair.first), palette.color(group, pair.second));
                QVERIFY2(ratio >= 4.5, qPrintable(QStringLiteral("%1 group %2 role %3: %4:1")
                    .arg(theme).arg(int(group)).arg(int(pair.first)).arg(ratio, 0, 'f', 3)));
            }
        QVERIFY(contrast(palette.color(QPalette::PlaceholderText), palette.color(QPalette::Base)) >= 4.5);
        QVERIFY(contrast(palette.color(QPalette::ToolTipText), palette.color(QPalette::ToolTipBase)) >= 4.5);
    }
    void informationValuesAreLiteralAndKeyboardSelectable() {
        ix::ImageDocument document;
        document.pixels = QImage(64, 48, QImage::Format_RGB32); document.pixels.fill(Qt::blue);
        document.path = QStringLiteral("/photos/<b>literal-name.png");
        probe([&] { ix::imageInformation(nullptr, document); }, [&](QDialog *dialog) {
            QLabel *value = nullptr;
            for (auto *label : dialog->findChildren<QLabel *>())
                if (label->text() == document.path) value = label;
            QVERIFY(value);
            QCOMPARE(value->textFormat(), Qt::PlainText);
            QVERIFY(value->textInteractionFlags().testFlag(Qt::TextSelectableByKeyboard));
            auto *metadata = dialog->findChild<QPlainTextEdit *>();
            QVERIFY(metadata && !metadata->accessibleName().isEmpty());
            capture(dialog, QStringLiteral("information"));
        });
    }
    void histogramFitsSmallWindows_data() { variants(); }
    void histogramFitsSmallWindows() {
        QFETCH(QString, theme); QFETCH(QString, language);
        ix::applyInterfaceLanguage(language); ix::applyInterfaceTheme(theme);
        QImage image(256, 64, QImage::Format_RGB32);
        for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(x, y, QColor(x, 255 - x, (x * 13) % 256));
        const auto key = image.cacheKey();
        probe([&] { ix::histogramDialog(nullptr, image); }, [&](QDialog *dialog) {
            dialog->resize(560, 360); QCoreApplication::processEvents();
            capture(dialog, QStringLiteral("histogram-%1-%2").arg(theme, language));
            QVERIFY2(dialog->width() <= 560, "Histogram forces a wider window than requested");
            auto *chart = dialog->findChild<QWidget *>(QStringLiteral("histogramChart"));
            QVERIFY(chart && !chart->accessibleName().isEmpty());
            QVERIFY(chart->width() >= 250);
            auto *buttons = dialog->findChild<QDialogButtonBox *>(); QVERIFY(buttons);
            QVERIFY(dialog->rect().contains(QRect(buttons->mapTo(dialog, QPoint()), buttons->size())));
        });
        QCOMPARE(image.cacheKey(), key);
    }
    void extensionActionsFitSmallWindows_data() { variants(); }
    void extensionActionsFitSmallWindows() {
#ifndef IX_TEST_EXTENSION_MANAGER
        QSKIP("Optional extension-manager module not built");
#else
        QFETCH(QString, theme); QFETCH(QString, language);
        ix::applyInterfaceLanguage(language); ix::applyInterfaceTheme(theme);
        probe([&] { ix::extensionManagerDialog(nullptr); }, [&](QDialog *dialog) {
            auto *tabs = dialog->findChild<QTabWidget *>(QStringLiteral("extensionSections"));
            QVERIFY(tabs); tabs->setCurrentIndex(1);
            dialog->resize(720, 500); QCoreApplication::processEvents();
            capture(dialog, QStringLiteral("extensions-%1-%2").arg(theme, language));
            QVERIFY2(dialog->width() <= 720, "Extension actions force an oversized dialog");
            for (const auto *name : {"installLocalExtension", "installCatalogExtension", "toggleExtension"}) {
                auto *button = dialog->findChild<QPushButton *>(QString::fromLatin1(name));
                QVERIFY(button && dialog->rect().contains(QRect(button->mapTo(dialog, QPoint()), button->size())));
            }
            auto *url = dialog->findChild<QLineEdit *>(QStringLiteral("extensionCatalogUrl"));
            QVERIFY(url); url->setFocus(); QTest::keyClick(url, Qt::Key_Tab);
            QVERIFY(dialog->focusWidget() && dialog->focusWidget() != url);
            QTest::keyClick(dialog, Qt::Key_Escape);
            QVERIFY(!dialog->isVisible());
        });
#endif
    }
    void editingFieldsHaveStableAccessibleNames() {
        ix::applyInterfaceLanguage(QStringLiteral("de"));
        ix::MainWindow window;
        auto *zoom = window.findChild<QComboBox *>(QStringLiteral("zoom"));
        QVERIFY(zoom && !zoom->accessibleName().isEmpty());
        ix::PipelineDialog pipeline({});
        for (const auto *name : {"pipelineSearch", "pipelineFormat", "pipelineInputs", "pipelineChain",
                                 "pipelineOutput", "pipelinePattern"}) {
            auto *field = pipeline.findChild<QWidget *>(QString::fromLatin1(name));
            QVERIFY(field);
            auto *accessible = QAccessible::queryAccessibleInterface(field);
            QVERIFY2(accessible && !accessible->text(QAccessible::Name).isEmpty(), name);
        }
    }
    void statusTipsNeverReplaceImageInformation() {
        ix::MainWindow window;
        window.show();
        const auto path = temporary_.filePath(QStringLiteral("status-image.png"));
        QImage pixels(32, 24, QImage::Format_RGB32); pixels.fill(Qt::blue);
        QVERIFY(pixels.save(path)); window.openPath(path);
        QTRY_VERIFY(!window.isBusy() && window.document().path == path);
        auto *bar = window.statusBar();
        const auto labels = bar->findChildren<QLabel *>();
        QStringList before;
        for (auto *label : labels) before.append(label->text());
        QSignalSpy messages(bar, &QStatusBar::messageChanged);
        for (const auto &tip : {QStringLiteral("Open image"), QString(), QStringLiteral("Resize")}) {
            QStatusTipEvent event(tip);
            QApplication::sendEvent(&window, &event);
            QVERIFY(bar->currentMessage().isEmpty());
        }
        QCOMPARE(messages.count(), 0);
        for (int i = 0; i < labels.size(); ++i) {
            QCOMPARE(labels[i]->text(), before[i]);
            QVERIFY(!labels[i]->isHidden());
        }
    }
    void mixedDirectoriesSkipUnsupportedFiles() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        for (const auto *name : {"one.png", "two.JPG", "clip.mp4", "notes.txt", "archive.zip", "fake.png"}) {
            QFile file(directory.filePath(QString::fromLatin1(name)));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write("fixture");
        }
        QVERIFY(QDir().mkpath(directory.filePath(QStringLiteral("subfolder.png"))));
        const auto result = ix::scanDirectory(directory.path(), {QStringLiteral("png"), QStringLiteral("jpg")}, 0, false);
        QCOMPARE(result.files.size(), 3);
        QVERIFY(result.files.contains(directory.filePath(QStringLiteral("fake.png"))));
        // Filename filtering is intentionally cheap. A corrupt supported file
        // remains a decoder error, never silently classified as a text file.
        for (const auto &path : result.files)
            QVERIFY(QStringList({QStringLiteral("png"), QStringLiteral("jpg")}).contains(QFileInfo(path).suffix().toLower()));
    }
};
QTEST_MAIN(UiQualityTests)
#include "ui_quality_ixview.moc"
