// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 IxView contributors
#include "core/codec_registry.h"
#include "core/app_storage.h"
#include "core/extension_storage.h"
#include "core/localization.h"
#include "core/plugin_store.h"
#include "ui/application.h"
#include "ui/main_window.h"
#include "ui/platform_support.h"
#include <QApplication>
#include <QColorSpace>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLibraryInfo>
#include <QSettings>
#include <QMessageBox>
#include <QStyleFactory>
#include <QTextStream>
#include <QTranslator>
#include <exception>
int main(int argc, char **argv) {
    ix::Application app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("IxView"));
    QCoreApplication::setApplicationName(QStringLiteral("IxView"));
    // Keep the existing storage identity across the product rename.
    QGuiApplication::setApplicationDisplayName(QStringLiteral("üxzView"));
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/ixview.svg")));
    QCoreApplication::setApplicationVersion(QStringLiteral(IXVIEW_VERSION));

    ix::configureDesktopAppearance();
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
    parser.addPositionalArgument(QStringLiteral("image"), QStringLiteral("Bilddatei"),
                                 QStringLiteral("[image]"));
    parser.process(app);
    if (parser.isSet(QStringLiteral("version"))) {
        QTextStream output(stdout);
        output.setEncoding(QStringConverter::Utf8);
        output << QGuiApplication::applicationDisplayName() << QLatin1Char(' ')
               << QCoreApplication::applicationVersion() << Qt::endl;
        return 0;
    }
    const auto storageError = ix::AppStorage::initialize({}, parser.isSet(QStringLiteral("portable")));
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
    ix::ExtensionStorage::initialize();
    ix::PluginStore::prepareStartup();
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
    ix::configureUiFont();
    QTranslator translator;
    if (translator.load(QStringLiteral("qtbase_") + language,
                        QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&translator);
    ix::MainWindow window;
    app.fileOpened = [&window](const QString &path) { window.openPath(path); };
    window.show();
    if (!parser.positionalArguments().isEmpty())
        window.openPath(parser.positionalArguments().first());
    return app.exec();
}
