#include "ui/common/PreferencesDialog.h"

#include "core/media/GpuVideoUploader.h"
#include "core/media/HwAccel.h"
#include "core/media/VideoDecoder.h"
#include "core/sources/VideoFileSource.h"
#include "ui/recording/ProgramRecorder.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>
#include <QVBoxLayout>

namespace hwaccel = prism::hwaccel;

namespace {

QString uploadPathName(prism::GpuVideoUploader::UploadPath path)
{
    using Path = prism::GpuVideoUploader::UploadPath;
    switch (path) {
    case Path::CudaInterop:  return QObject::tr("CUDA interop (zero-copy)");
    case Path::VaapiDmaBuf:  return QObject::tr("VAAPI dma-buf (zero-copy)");
    case Path::D3d11Interop: return QObject::tr("D3D11 interop (zero-copy)");
    case Path::CpuRoundTrip: return QObject::tr("system memory upload");
    case Path::None:         break;
    }
    return QObject::tr("no video played yet");
}

void selectData(QComboBox *combo, const QString &value)
{
    const int index = combo->findData(value);
    combo->setCurrentIndex(index >= 0 ? index : 0);
}

} // namespace

PreferencesDialog::PreferencesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Playback & Hardware"));
    setMinimumWidth(460);

    // ── Decoding ──
    m_decodeCombo = new QComboBox(this);
    m_decodeCombo->addItem(tr("Auto"), QStringLiteral("auto"));
    m_decodeCombo->addItem(tr("Software"), QStringLiteral("software"));
    for (const hwaccel::Backend backend : hwaccel::availableDecodeBackends()) {
        m_decodeCombo->addItem(QString::fromLatin1(hwaccel::name(backend)),
                               QStringLiteral("hw:") + hwaccel::id(backend));
    }
    selectData(m_decodeCombo, VideoDecoder::modeSetting());

    m_decodeWarning = new QLabel(this);
    m_decodeWarning->setWordWrap(true);
    m_decodeWarning->setStyleSheet(QStringLiteral("color: #e0a045;"));

    m_zeroCopyCombo = new QComboBox(this);
    m_zeroCopyCombo->addItem(tr("Auto"), QStringLiteral("auto"));
    m_zeroCopyCombo->addItem(tr("On"), QStringLiteral("on"));
    m_zeroCopyCombo->addItem(tr("Off"), QStringLiteral("off"));
    selectData(m_zeroCopyCombo,
               QSettings().value(QStringLiteral("playback/zeroCopy"), QStringLiteral("auto")).toString());

    auto *zeroCopyNote = new QLabel(
        tr("Hands decoded frames to OpenGL without copying them through system memory. Auto "
           "uses it where it has been verified. Takes effect after restarting Prism."), this);
    zeroCopyNote->setWordWrap(true);
    zeroCopyNote->setEnabled(false);

    auto *decodeBox = new QGroupBox(tr("Video decoding"), this);
    auto *decodeForm = new QFormLayout(decodeBox);
    decodeForm->addRow(tr("Decoder:"), m_decodeCombo);
    decodeForm->addRow(QString(), m_decodeWarning);
    decodeForm->addRow(tr("Zero-copy:"), m_zeroCopyCombo);
    decodeForm->addRow(QString(), zeroCopyNote);

    // ── Recording ──
    m_encoderCombo = new QComboBox(this);
    for (const ProgramRecorder::EncoderOption &option : ProgramRecorder::availableEncoders())
        m_encoderCombo->addItem(option.label, option.id);
    selectData(m_encoderCombo, ProgramRecorder::encoderSetting());

    auto *recordBox = new QGroupBox(tr("Recording"), this);
    auto *recordForm = new QFormLayout(recordBox);
    recordForm->addRow(tr("H.264 encoder:"), m_encoderCombo);

    // ── Status ──
    QString status = tr("Last frame upload: %1")
                         .arg(uploadPathName(prism::GpuVideoUploader::lastUploadPath()));
    const QString decline = prism::GpuVideoUploader::lastZeroCopyDeclineReason();
    if (!decline.isEmpty())
        status += QLatin1Char('\n') + tr("Zero-copy unavailable: %1").arg(decline);
    if (VideoDecoder::hardwareFallbackCount() > 0) {
        status += QLatin1Char('\n')
            + tr("Hardware decode fell back to software %n time(s). Last: %1", nullptr,
                 int(VideoDecoder::hardwareFallbackCount()))
                  .arg(VideoDecoder::lastHardwareFailure());
    }
    auto *statusLabel = new QLabel(status, this);
    statusLabel->setWordWrap(true);
    statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        apply();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *layout = new QVBoxLayout(this);
    layout->addWidget(decodeBox);
    layout->addWidget(recordBox);
    layout->addWidget(statusLabel);
    layout->addStretch();
    layout->addWidget(buttons);

    connect(m_decodeCombo, &QComboBox::currentIndexChanged, this,
            &PreferencesDialog::updateDecodeWarning);
    updateDecodeWarning();
}

void PreferencesDialog::updateDecodeWarning()
{
    const QString value = m_decodeCombo->currentData().toString();
    QString warning;
    if (value.startsWith(QLatin1String("hw:"))) {
        const hwaccel::Backend backend = hwaccel::backendFromId(value.mid(3));
        const hwaccel::RenderMatchInfo match = hwaccel::describeRenderMatch(backend);
        if (match.match == hwaccel::RenderMatch::Mismatch) {
            warning = tr("%1 decodes on %2 while Prism draws on %3, so every frame crosses "
                         "between GPUs. Auto avoids this.")
                          .arg(QString::fromLatin1(hwaccel::name(backend)),
                               match.decodeGpu.isEmpty() ? tr("another GPU") : match.decodeGpu,
                               match.renderGpu.isEmpty() ? tr("this GPU") : match.renderGpu);
        }
    }
    m_decodeWarning->setText(warning);
    m_decodeWarning->setVisible(!warning.isEmpty());
}

void PreferencesDialog::apply()
{
    const QString decode = m_decodeCombo->currentData().toString();
    if (decode != VideoDecoder::modeSetting()) {
        VideoDecoder::storeModeSetting(decode);
        VideoDecoder::applyModeSetting(decode);
        VideoFileSource::reopenAll();
    }
    QSettings().setValue(QStringLiteral("playback/zeroCopy"),
                         m_zeroCopyCombo->currentData().toString());
    ProgramRecorder::setEncoderSetting(m_encoderCombo->currentData().toString());
}
