#include "app/dialogs/PlaybackSettings.h"

#include "core/media/GpuVideoUploader.h"
#include "core/media/VideoDecoder.h"
#include "app/recording/ProgramRecorder.h"

#include <QSettings>
#include <QVariantMap>

PlaybackSettings::PlaybackSettings(QObject *parent)
    : QObject(parent)
{
}

QString PlaybackSettings::zeroCopy() const
{
    return QSettings().value(QStringLiteral("playback/zeroCopy"), QStringLiteral("auto")).toString();
}

void PlaybackSettings::setZeroCopy(const QString &mode)
{
    if (mode == zeroCopy())
        return;
    QSettings().setValue(QStringLiteral("playback/zeroCopy"), mode);
    emit changed();
}

QVariantList PlaybackSettings::codecs() const
{
    QVariantList out;
    for (const ProgramRecorder::CodecOption &c : ProgramRecorder::availableCodecs())
        out.append(QVariantMap{{"id", c.id}, {"label", c.label}});
    return out;
}

QString PlaybackSettings::codec() const { return ProgramRecorder::codecSetting(); }

void PlaybackSettings::setCodec(const QString &id)
{
    if (id == codec())
        return;
    ProgramRecorder::setCodecSetting(id);
    emit changed();
}

QVariantList PlaybackSettings::containers() const
{
    return {QVariantMap{{"id", QStringLiteral("mkv")}, {"label", QStringLiteral("MKV")}},
            QVariantMap{{"id", QStringLiteral("mp4")}, {"label", QStringLiteral("MP4")}}};
}

QString PlaybackSettings::container() const { return ProgramRecorder::containerSetting(); }

void PlaybackSettings::setContainer(const QString &id)
{
    if (id == container())
        return;
    ProgramRecorder::setContainerSetting(id);
    emit changed();
}

QVariantList PlaybackSettings::encoders() const
{
    QVariantList out;
    for (const ProgramRecorder::EncoderOption &e : ProgramRecorder::availableEncoders())
        out.append(QVariantMap{{"id", e.id}, {"label", e.label}});
    return out;
}

QString PlaybackSettings::encoder() const { return ProgramRecorder::encoderSetting(); }

void PlaybackSettings::setEncoder(const QString &id)
{
    if (id == encoder())
        return;
    ProgramRecorder::setEncoderSetting(id);
    emit changed();
}

QString PlaybackSettings::uploadStatus() const
{
    using Path = prism::GpuVideoUploader::UploadPath;
    QString path;
    switch (prism::GpuVideoUploader::lastUploadPath()) {
    case Path::CudaInterop:  path = tr("CUDA interop (zero-copy)"); break;
    case Path::VaapiDmaBuf:  path = tr("VAAPI dma-buf (zero-copy)"); break;
    case Path::D3d11Interop: path = tr("D3D11 interop (zero-copy)"); break;
    case Path::CpuRoundTrip: path = tr("system memory upload"); break;
    case Path::None:         path = tr("no video played yet"); break;
    }
    QString status = tr("Last frame upload: %1").arg(path);
    const QString decline = prism::GpuVideoUploader::lastZeroCopyDeclineReason();
    if (!decline.isEmpty())
        status += QLatin1Char('\n') + tr("Zero-copy unavailable: %1").arg(decline);
    if (VideoDecoder::hardwareFallbackCount() > 0) {
        status += QLatin1Char('\n')
            + tr("Hardware decode fell back to software %n time(s). Last: %1", nullptr,
                 int(VideoDecoder::hardwareFallbackCount()))
                  .arg(VideoDecoder::lastHardwareFailure());
    }
    return status;
}
