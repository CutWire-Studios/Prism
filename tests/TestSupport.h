#pragma once

#include "ui/ProgramRecorder.h"
#include "ui/VideoWidget.h"

#include <QDir>
#include <QFile>
#include <QImage>
#include <QMap>
#include <QtMath>
#include <cstring>
#include <vector>
#include <zip.h>

namespace TestSupport {

inline QImage makeSolidImage(int w, int h, const QColor &color = Qt::red) {
    QImage img(w, h, QImage::Format_RGB888);
    img.fill(color);
    return img;
}

inline bool writeSineWav(const QString &path, double frequencyHz = 440.0,
                         int sampleRate = 44100, int channels = 2,
                         double durationSec = 1.0) {
    const int sampleCount = static_cast<int>(sampleRate * durationSec);
    const int dataBytes   = sampleCount * channels * static_cast<int>(sizeof(qint16));

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    auto writeU32 = [&](quint32 v) { file.write(reinterpret_cast<const char *>(&v), 4); };
    auto writeU16 = [&](quint16 v) { file.write(reinterpret_cast<const char *>(&v), 2); };

    writeU32(0x46464952u); // "RIFF"
    writeU32(static_cast<quint32>(36 + dataBytes));
    writeU32(0x45564157u); // "WAVE"
    writeU32(0x20746D66u); // "fmt "
    writeU32(16);
    writeU16(1); // PCM
    writeU16(static_cast<quint16>(channels));
    writeU32(static_cast<quint32>(sampleRate));
    writeU32(static_cast<quint32>(static_cast<quint32>(sampleRate) * static_cast<quint32>(channels)
                                 * static_cast<quint32>(sizeof(qint16))));
    writeU16(static_cast<quint16>(channels * sizeof(qint16)));
    writeU16(16);
    writeU32(0x61746164u); // "data"
    writeU32(static_cast<quint32>(dataBytes));

    for (int i = 0; i < sampleCount; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        const double sample = qSin(2.0 * M_PI * frequencyHz * t);
        const qint16 pcm = static_cast<qint16>(sample * 30000.0);
        for (int ch = 0; ch < channels; ++ch) {
            file.write(reinterpret_cast<const char *>(&pcm), sizeof(pcm));
        }
    }

    return true;
}

inline QString encodeSampleMkv(const QString &dirPath, int frameCount = 30,
                               const QColor &color = Qt::blue) {
    ProgramRecorder recorder;
    const QString path = QDir(dirPath).filePath(QStringLiteral("sample.mkv"));
    if (!recorder.startRecording(path, QStringLiteral("test"), false)) {
        return {};
    }

    const QImage frame = makeSolidImage(VideoWidget::kProgramWidth,
                                        VideoWidget::kProgramHeight, color);
    for (int i = 0; i < frameCount; ++i) {
        recorder.submitFrame(frame);
    }
    recorder.stopRecording();
    return path;
}

inline bool writePng(const QString &dirPath, const QString &name, const QColor &color = Qt::green) {
    return makeSolidImage(64, 48, color).save(QDir(dirPath).filePath(name));
}

inline bool createZip(const QString &zipPath, const QMap<QString, QByteArray> &entries) {
    if (QFile::exists(zipPath) && !QFile::remove(zipPath)) {
        return false;
    }

    const QByteArray pathUtf8 = zipPath.toUtf8();
    zip_t *archive = zip_open(pathUtf8.constData(), ZIP_CREATE | ZIP_TRUNCATE, nullptr);
    if (!archive) {
        return false;
    }

    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        const QByteArray nameUtf8 = it.key().toUtf8();
        zip_source_t *source = zip_source_buffer(archive, it.value().constData(),
                                                 static_cast<zip_uint64_t>(it.value().size()),
                                                 0);
        if (!source) {
            zip_close(archive);
            return false;
        }
        if (zip_file_add(archive, nameUtf8.constData(), source, ZIP_FL_ENC_UTF_8) < 0) {
            zip_source_free(source);
            zip_close(archive);
            return false;
        }
    }

    const int err = zip_close(archive);
    return err == 0;
}

} // namespace TestSupport
