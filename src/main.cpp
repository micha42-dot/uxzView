#include "core/image_storage.h"
#include <QtConcurrent>
// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "core/codec_registry.h"
#include "core/app_storage.h"
#include "core/extension_storage.h"
#include "core/localization.h"
#include "core/plugin_store.h"
#include "ui/application.h"
#include "ui/appearance.h"
#include "ui/interface_language.h"
#include "ui/main_window.h"
#include "ui/platform_support.h"
#include <QApplication>
#include <QColorSpace>
#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibrary>
#include <QPointer>
#include <QSettings>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTextStream>
#include <exception>
#include <initializer_list>
int main(int argc, char **argv) {
    const bool profileStartup = qEnvironmentVariableIntValue("IX_PROFILE_IMAGE_LOAD") != 0;
    QElapsedTimer startupTimer;
    if (profileStartup) {
        startupTimer.start();
        qInfo("ix-startup main-entry");
    }
    qint64 previousStage = 0;
    const auto startupStage = [&](const char *name) {
        if (!profileStartup) return;
        const auto now = startupTimer.nsecsElapsed();
        qInfo("ix-startup %s %.3f ms total %.3f ms", name,
              (now - previousStage) / 1e6, now / 1e6);
        previousStage = now;
    };
    ix::Application app(argc, argv);
    startupStage("application");
    QCoreApplication::setOrganizationName(QStringLiteral("IxView"));
    QCoreApplication::setApplicationName(QStringLiteral("IxView"));
    // Keep the existing storage identity across the product rename.
    QGuiApplication::setApplicationDisplayName(QStringLiteral("üxzView"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/ixview.svg")));
    QCoreApplication::setApplicationVersion(QStringLiteral(IXVIEW_VERSION));

    ix::configureDesktopAppearance();
    startupStage("desktop-appearance");
    QGuiApplication::setDesktopFileName(QStringLiteral("org.ixview.IxView"));
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("üxzView - Image viewer"));
    parser.addHelpOption();
    parser.addOption(QCommandLineOption(QStringList{QStringLiteral("v"), QStringLiteral("version")},
                                        QStringLiteral("Print application version")));
    parser.addOption(QCommandLineOption(QStringLiteral("portable"),
                                        QStringLiteral("Use a portable profile beside the application")));
    parser.addOption(QCommandLineOption(QStringLiteral("storage-info"),
                                        QStringLiteral("Print active storage locations as JSON")));
    parser.addOption(QCommandLineOption(QStringLiteral("language"),
                                        QStringLiteral("Interface language: de, en, ko, ja, fr"),
                                        QStringLiteral("code")));
    parser.addOption(QCommandLineOption(
        QStringLiteral("probe-image"),
        QStringLiteral("Decode an image and print JSON without opening a window"), QStringLiteral("path")));
    parser.addOption(QCommandLineOption(QStringLiteral("probe-modules"),
                                        QStringLiteral("Check compiled optional modules and print JSON")));
    parser.addOption(QCommandLineOption(QStringLiteral("memory-info"),
                                        QStringLiteral("Print the active image memory budget as JSON")));
    parser.addPositionalArgument(QStringLiteral("image"), QStringLiteral("Bilddatei"),
                                 QStringLiteral("[image]"));
    parser.process(app);
    startupStage("arguments");
    if (parser.isSet(QStringLiteral("version"))) {
        QTextStream output(stdout);
        output.setEncoding(QStringConverter::Utf8);
        output << QGuiApplication::applicationDisplayName() << QLatin1Char(' ')
               << QCoreApplication::applicationVersion() << Qt::endl;
        return 0;
    }
    if (parser.isSet(QStringLiteral("probe-modules"))) {
        QJsonObject result;
        bool ok = true;
        [[maybe_unused]] const auto probe = [&](const QString &id, const QString &name,
                               std::initializer_list<const char *> symbols) {
            QLibrary library(ix::optionalModulePath(name));
            bool loaded = library.load();
            QJsonArray found;
            if (loaded) {
                for (const auto symbol : symbols) {
                    if (library.resolve(symbol))
                        found.append(QString::fromLatin1(symbol));
                    else {
                        loaded = false;
                        break;
                    }
                }
            }
            QJsonObject module{{QStringLiteral("path"), name},
                               {QStringLiteral("symbols"), found},
                               {QStringLiteral("loaded"), loaded}};
            if (!loaded) module.insert(QStringLiteral("error"), library.errorString());
            result.insert(id, module);
            ok &= loaded;
        };
#ifdef IX_RAW_PREVIEW_MODULE
        probe(QStringLiteral("ixrawpreview"), QStringLiteral(IX_RAW_PREVIEW_MODULE), {"ixReadRawPreviewSized"});
#endif
#ifdef IX_PDF_MODULE
        probe(QStringLiteral("ixpdf"), QStringLiteral(IX_PDF_MODULE), {"ixReadPdf", "ixReadPdfPreview"});
#endif
#ifdef IX_EXTENSIONS_MODULE
        probe(QStringLiteral("ixextensions"), QStringLiteral(IX_EXTENSIONS_MODULE),
              {"ixOpenExtensionManager", "ixOpenFunctionExtensionManager"});
#endif
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        return ok ? 0 : 2;
    }
    const auto storageError = ix::AppStorage::initialize({}, parser.isSet(QStringLiteral("portable")));
    startupStage("storage");
    if (!storageError.isEmpty()) {
        QTextStream(stderr) << storageError << Qt::endl;
        if (!parser.isSet(QStringLiteral("storage-info")) && !parser.isSet(QStringLiteral("probe-image")))
            QMessageBox::critical(nullptr, QStringLiteral("üxzView"), storageError);
        return 3;
    }
    if (parser.isSet(QStringLiteral("storage-info"))) {
        const QJsonObject storage{{QStringLiteral("portable"), ix::AppStorage::isPortable()},
                                  {QStringLiteral("home"), ix::AppStorage::applicationHome()},
                                  {QStringLiteral("data"), ix::AppStorage::dataRoot()},
                                  {QStringLiteral("cache"), ix::AppStorage::cacheRoot()},
                                  {QStringLiteral("tools"), ix::ExtensionStorage::requestedRoot(ix::ExtensionStorage::Domain::Tools)},
                                  {QStringLiteral("settings"), QSettings().fileName()}};
        QTextStream(stdout) << QJsonDocument(storage).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
    ix::initializeImageMemoryLimit();
    ix::configureImageStorageFromSettings();
    startupStage("image-memory");
    const auto swapRoot = QDir(ix::AppStorage::cacheRoot()).filePath(QStringLiteral("swap"));
    if (QFileInfo::exists(swapRoot)) (void)QtConcurrent::run([swapRoot] { ix::cleanupStaleImageStorage(swapRoot); });
    if (parser.isSet(QStringLiteral("memory-info"))) {
        const QJsonObject result{{QStringLiteral("physicalBytes"), double(ix::physicalMemoryBytes())},
            {QStringLiteral("memoryLimitMiB"), double(ix::imageMemoryLimit() / ix::MiB)},
            {QStringLiteral("axisLimit"), ix::ImageAxisLimit},
            {QStringLiteral("swapEnabled"), ix::imageStorageOptions().enabled},
            {QStringLiteral("swapLimitMiB"), double(ix::imageStorageOptions().maxBytes / ix::MiB)}};
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        return 0;
    }
    ix::ExtensionStorage::initialize();
    ix::PluginStore::prepareStartup();
    startupStage("extensions");
    if (parser.isSet(QStringLiteral("probe-image"))) {
        QJsonObject result;
        int status = 0;
        try {
            const auto read = ix::CodecRegistry().read(parser.value(QStringLiteral("probe-image")));
            if (!read.ok()) {
                result.insert(QStringLiteral("error"), read.error);
                status = 2;
            } else {
                result.insert(QStringLiteral("width"), read.document.pixels.width());
                result.insert(QStringLiteral("height"), read.document.pixels.height());
                result.insert(QStringLiteral("pages"), read.document.pageCount);
                result.insert(QStringLiteral("format"), QString::fromLatin1(read.document.format));
                result.insert(QStringLiteral("icc"), read.document.pixels.colorSpace().isValid());
            }
        } catch (const std::exception &error) {
            result.insert(QStringLiteral("error"), QString::fromUtf8(error.what()));
            status = 2;
        }
        QTextStream(stdout) << QJsonDocument(result).toJson(QJsonDocument::Compact) << Qt::endl;
        return status;
    }
    auto language = parser.isSet(QStringLiteral("language"))
                        ? parser.value(QStringLiteral("language"))
                        : QSettings().value(QStringLiteral("ui/language"), QStringLiteral("de")).toString();
    if (!ix::setUiLanguage(language)) {
        if (parser.isSet(QStringLiteral("language")))
            parser.showHelp(2);
        language = QStringLiteral("de");
        ix::setUiLanguage(language);
    }
    // Process-wide collation stays fixed once workers exist. Interface widgets
    // use their own locale for subsequent live language changes.
    QLocale::setDefault(ix::uiLocale());
    ix::applyInterfaceLanguage(language);
    startupStage("language");
    ix::applyInterfaceTheme(QSettings().value(QStringLiteral("ui/theme"), QStringLiteral("system")).toString());
    startupStage("theme");
    ix::MainWindow window;
    startupStage("main-window");
    app.fileOpened = [window = QPointer<ix::MainWindow>(&window)](const QString &path) {
        if (window) window->openPath(path);
    };
    window.show();
    startupStage("show-window");
    if (!parser.positionalArguments().isEmpty())
        window.openPath(parser.positionalArguments().first());
    startupStage("open-request");
    return app.exec();
}
