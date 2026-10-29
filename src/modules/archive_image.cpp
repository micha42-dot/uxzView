// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only comic/image archives. Each handler owns its archive and current page;
// no extraction, shared decoder state or archive work on ordinary image paths.
#include <KArchiveDirectory>
#include <KArchiveFile>
#include <KZip>
#include <QBuffer>
#include <QCollator>
#include <QFile>
#include <QFileInfo>
#include <QImageIOHandler>
#include <QImageReader>
#include <QSet>
#include <QtEndian>
#include <algorithm>
#include <memory>

namespace {
constexpr qint64 EntryLimit = 128 * 1024 * 1024;
constexpr int FileLimit = 10000;

// KArchive closes its device on destruction. Qt owns the input; keep that
// device open so a rejected archive cannot break Qt's other format probes.
class BorrowedDevice final : public QIODevice {
    QIODevice *source_;
  public:
    explicit BorrowedDevice(QIODevice *source) : source_(source) {
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        seek(0);
    }
    qint64 size() const override { return source_->size(); }
    bool seek(qint64 position) override { return source_->seek(position) && QIODevice::seek(position); }
  protected:
    qint64 readData(char *data, qint64 size) override { return source_->read(data, size); }
    qint64 writeData(const char *, qint64) override { return -1; }
};

// Bound the directory before KZip allocates its tree. ZIP64, multi-volume,
// encrypted and self-extracting archives are deliberately outside this reader.
bool boundedZip(QIODevice *device) {
    if (!device || device->isSequential() || device->size() < 22 || device->size() > 0x7fffffff)
        return false;
    const auto original = device->pos();
    const auto check = [&] {
        const auto start = std::max<qint64>(0, device->size() - 65557);
        if (!device->seek(start)) return false;
        const auto tail = device->read(65557);
        for (auto end = tail.lastIndexOf(QByteArray("PK\5\6", 4)); end >= 0;
             end = end > 0 ? tail.lastIndexOf(QByteArray("PK\5\6", 4), end - 1) : -1) {
            if (end + 22 > tail.size()) continue;
            const auto *p = reinterpret_cast<const uchar *>(tail.constData() + end);
            const auto u16 = [&](int offset) { return qFromLittleEndian<quint16>(p + offset); };
            const auto u32 = [&](int offset) { return qFromLittleEndian<quint32>(p + offset); };
            if (end + 22 + u16(20) != tail.size()) continue;
            const auto count = u16(10);
            const qint64 directorySize = u32(12), directoryOffset = u32(16);
            if (u16(4) || u16(6) || !count || count > FileLimit || u16(8) != count ||
                directorySize > 8 * 1024 * 1024 || directoryOffset + directorySize != start + end ||
                !device->seek(0) || device->read(4) != QByteArray("PK\3\4", 4) ||
                !device->seek(directoryOffset)) return false;
            const auto directoryEnd = directoryOffset + directorySize;
            QSet<QByteArray> names, directories;
            for (int n = 0; n < count; ++n) {
                if (device->pos() + 46 > directoryEnd) return false;
                const auto header = device->read(46);
                if (header.size() != 46 || !header.startsWith(QByteArray("PK\1\2", 4))) return false;
                const auto *h = reinterpret_cast<const uchar *>(header.constData());
                const auto nameSize = qFromLittleEndian<quint16>(h + 28);
                const auto extraSize = qFromLittleEndian<quint16>(h + 30);
                const auto commentSize = qFromLittleEndian<quint16>(h + 32);
                if (qFromLittleEndian<quint16>(h + 8) & 1 ||
                    qFromLittleEndian<quint16>(h + 34) || nameSize > 4096 || !nameSize ||
                    qFromLittleEndian<quint32>(h + 20) > EntryLimit ||
                    qFromLittleEndian<quint32>(h + 24) > EntryLimit ||
                    qFromLittleEndian<quint32>(h + 42) >= directoryOffset ||
                    device->pos() + nameSize + extraSize + commentSize > directoryEnd) return false;
                auto name = device->read(nameSize).replace('\\', '/');
                const auto parts = name.split('/');
                if (name.contains('\0') || name.startsWith('/') || name.contains(':') ||
                    parts.contains("..") || parts.size() > 32 || names.contains(name)) return false;
                names.insert(name);
                QByteArray prefix;
                for (qsizetype component = 0; component + 1 < parts.size(); ++component) {
                    prefix += parts.at(component); prefix += '/';
                    directories.insert(prefix);
                    // A small entry count can still create a much larger tree
                    // through implicit parents. Bound those before KZip parses.
                    if (directories.size() > FileLimit) return false;
                }
                if (!device->seek(device->pos() + extraSize + commentSize)) return false;
            }
            return device->pos() == directoryEnd;
        }
        return false;
    };
    const bool valid = check();
    return device->seek(original) && valid;
}

class ArchiveHandler final : public QImageIOHandler {
    struct Page { QString path; const KArchiveFile *file; };
    mutable std::unique_ptr<BorrowedDevice> input_;
    mutable std::unique_ptr<KZip> archive_;
    mutable QList<Page> pages_;
    mutable std::unique_ptr<QBuffer> buffer_;
    mutable std::unique_ptr<QImageReader> reader_;
    mutable bool attempted_ = false;
    int current_ = 0;
    bool consumed_ = false;
    QSize scaled_;

    bool open() const {
        if (attempted_) return archive_ && !pages_.isEmpty();
        attempted_ = true;
        if (!boundedZip(device())) return false;
        input_ = std::make_unique<BorrowedDevice>(device());
        archive_ = std::make_unique<KZip>(input_.get());
        if (!archive_->open(QIODevice::ReadOnly)) return false;
        auto formats = QImageReader::supportedImageFormats();
        formats.removeAll("zip"); formats.removeAll("cbz");
        int visited = 0;
        const auto collect = [&](const auto &self, const KArchiveDirectory *directory,
                                 const QString &prefix, int depth) -> bool {
            if (!directory || depth > 32) return false;
            for (const auto &name : directory->entries()) {
                if (++visited > FileLimit * 2) return false;
                const auto *entry = directory->entry(name);
                if (!entry || !entry->symLinkTarget().isEmpty() || name.startsWith(QLatin1Char('.')) ||
                    name == QStringLiteral("__MACOSX")) continue;
                const auto path = prefix + name;
                if (entry->isDirectory()) {
                    if (!self(self, static_cast<const KArchiveDirectory *>(entry), path + QLatin1Char('/'), depth + 1))
                        return false;
                } else if (entry->isFile() && formats.contains(QFileInfo(name).suffix().toLower().toLatin1())) {
                    const auto *file = static_cast<const KArchiveFile *>(entry);
                    if (file->size() > 0 && file->size() <= EntryLimit) pages_.append({path, file});
                }
            }
            return true;
        };
        if (!collect(collect, archive_->directory(), {}, 0)) { pages_.clear(); return false; }
        QCollator collator(QLocale(QLocale::English, QLocale::UnitedStates));
        collator.setNumericMode(true);
        collator.setCaseSensitivity(Qt::CaseInsensitive);
        std::sort(pages_.begin(), pages_.end(), [&](const Page &a, const Page &b) {
            const int order = collator.compare(a.path, b.path);
            return order ? order < 0 : a.path < b.path;
        });
        return !pages_.isEmpty();
    }
    bool prepare() const {
        if (reader_) return true;
        if (!open() || current_ < 0 || current_ >= pages_.size()) return false;
        const auto &page = pages_[current_];
        std::unique_ptr<QIODevice> input(page.file->createDevice());
        if (!input) return false;
        auto bytes = input->read(EntryLimit + 1);
        if (bytes.size() != page.file->size() || bytes.size() > EntryLimit) return false;
        auto buffer = std::make_unique<QBuffer>();
        buffer->setData(std::move(bytes)); buffer->open(QIODevice::ReadOnly);
        auto reader = std::make_unique<QImageReader>(buffer.get(), QFileInfo(page.path).suffix().toLatin1().toLower());
        // Never probe a nested ZIP under an image extension.
        reader->setDecideFormatFromContent(false);
        const auto size = reader->size();
        if (!reader->canRead() || !size.isValid() || size.width() > 100000 || size.height() > 100000 ||
            qint64(size.width()) * size.height() * 8 > 512 * 1024 * 1024) return false;
        if (scaled_.isValid()) reader->setScaledSize(scaled_);
        buffer_ = std::move(buffer); reader_ = std::move(reader);
        return true;
    }
  public:
    bool canRead() const override { return !consumed_ && open(); }
    int imageCount() const override { return open() ? int(pages_.size()) : 0; }
    int currentImageNumber() const override { return current_; }
    bool jumpToImage(int page) override {
        if (!open() || page < 0 || page >= pages_.size()) return false;
        reader_.reset(); buffer_.reset(); current_ = page; consumed_ = false;
        return true;
    }
    bool jumpToNextImage() override { return jumpToImage(current_ + 1); }
    bool read(QImage *image) override {
        if (!image || consumed_ || !prepare()) return false;
        *image = reader_->read(); consumed_ = true;
        if (image->isNull()) return false;
        image->setText(QStringLiteral("ArchiveEntry"), pages_[current_].path);
        return true;
    }
    bool supportsOption(ImageOption option) const override {
        return option == Size || option == ScaledSize || option == ImageTransformation;
    }
    QVariant option(ImageOption option) const override {
        if (option == ScaledSize) return scaled_;
        if (option == Size && prepare()) return reader_->size();
        if (option == ImageTransformation && prepare()) return int(reader_->transformation());
        return {};
    }
    void setOption(ImageOption option, const QVariant &value) override {
        if (option == ScaledSize) {
            scaled_ = value.toSize();
            if (reader_) reader_->setScaledSize(scaled_);
        }
    }
};
class ArchivePlugin final : public QImageIOPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QImageIOHandlerFactoryInterface_iid FILE "archive_image.json")
  public:
    Capabilities capabilities(QIODevice *device, const QByteArray &format) const override {
        if (format == "zip" || format == "cbz") return CanRead;
        // KRA/ORA are also ZIP containers; leave their content sniffing to their
        // own handlers. This plugin recognizes archives by extension/explicit key.
        if (format.isEmpty()) {
            const auto *file = qobject_cast<QFile *>(device);
            const auto suffix = file ? QFileInfo(file->fileName()).suffix().toLower() : QString();
            if (suffix == QStringLiteral("zip") || suffix == QStringLiteral("cbz")) return CanRead;
        }
        return {};
    }
    QImageIOHandler *create(QIODevice *device, const QByteArray &format) const override {
        auto *handler = new ArchiveHandler;
        handler->setDevice(device);
        handler->setFormat(format.isEmpty() ? QByteArray("zip") : format);
        return handler;
    }
};
} // namespace
#include "archive_image.moc"
