// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QSize>
#include <QtEndian>
namespace ix::test {
inline QByteArray apngFixture(int count, QSize size = {128, 128}, quint16 delay = 1, quint32 plays = 0) {
    auto be32 = [](quint32 value) {
        QByteArray bytes(4, Qt::Uninitialized);
        qToBigEndian(value, reinterpret_cast<uchar *>(bytes.data()));
        return bytes;
    };
    auto chunk = [&](const QByteArray &type, const QByteArray &data) {
        quint32 crc = 0xffffffff;
        for (auto byte : type + data) {
            crc ^= quint8(byte);
            for (int bit = 0; bit < 8; ++bit)
                crc = (crc >> 1) ^ (0xedb88320 & quint32(-qint32(crc & 1)));
        }
        return be32(data.size()) + type + data + be32(~crc);
    };
    QByteArray png = QByteArray::fromHex("89504e470d0a1a0a");
    png += chunk("IHDR", be32(size.width()) + be32(size.height()) + QByteArray::fromHex("0806000000"));
    png += chunk("acTL", be32(count) + be32(plays));
    quint32 sequence = 0;
    for (int frame = 0; frame < count; ++frame) {
        QByteArray timing(4, Qt::Uninitialized);
        qToBigEndian(delay, reinterpret_cast<uchar *>(timing.data()));
        qToBigEndian(quint16(100), reinterpret_cast<uchar *>(timing.data() + 2));
        png += chunk("fcTL", be32(sequence++) + be32(size.width()) + be32(size.height()) + be32(0) +
                                 be32(0) + timing + QByteArray(2, 0));
        QByteArray row(1, 0);
        for (int x = 0; x < size.width(); ++x) {
            row.append(char((frame * 41 + 17) % 256));
            row.append(char((frame * 13 + 34) % 256));
            row.append(char((frame * 7 + 51) % 256));
            row.append(char(255));
        }
        QByteArray raw;
        for (int y = 0; y < size.height(); ++y)
            raw += row;
        const auto compressed = qCompress(raw).mid(4);
        png += frame == 0 ? chunk("IDAT", compressed) : chunk("fdAT", be32(sequence++) + compressed);
    }
    return png + chunk("IEND", {});
}
} // namespace ix::test
