#include "core/media/AudioInputCapture.h"
#include "core/media/AudioInputMixRegistry.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QMediaDevices>
#include <QDebug>

#include <algorithm>

AudioInputCapture::AudioInputCapture(QObject *parent)
    : QObject(parent)
{
    m_pullTimer.setInterval(20);
    connect(&m_pullTimer, &QTimer::timeout, this, [this]() { pullInput(); });
}

AudioInputCapture::~AudioInputCapture() {
    stop();
}

void AudioInputCapture::setTargetOutputDeviceId(const QString &deviceId) {
    if (m_targetOutputDeviceId == deviceId)
        return;
    if (isRunning())
        AudioInputMixRegistry::clearDevice(m_targetOutputDeviceId);
    m_targetOutputDeviceId = deviceId;
}

void AudioInputCapture::setVolumePercent(int volumePercent) {
    m_volumePercent = std::clamp(volumePercent, 0, 100);
}

void AudioInputCapture::setEffectChain(const QVector<AudioEffectRef> &effects) {
    m_effectChain.setEffects(effects);
}

bool AudioInputCapture::isRunning() const {
    return m_source != nullptr;
}

bool AudioInputCapture::start() {
    stop();

    QAudioFormat format;
    format.setSampleRate(AudioDecoder::kOutputSampleRate);
    format.setChannelCount(AudioDecoder::kOutputChannels);
    format.setSampleFormat(QAudioFormat::Float);

    QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (!m_inputDeviceId.isEmpty()) {
        for (const QAudioDevice &dev : QMediaDevices::audioInputs()) {
            if (QString::fromUtf8(dev.id()) == m_inputDeviceId) {
                device = dev;
                break;
            }
        }
    }
    if (device.isNull()) {
        qWarning() << "AudioInputCapture: no audio input device available";
        return false;
    }
    m_needsInputResample = false;
    if (!device.isFormatSupported(format)) {
        const QAudioFormat preferred = device.preferredFormat();
        if (!preferred.isValid() || preferred.sampleRate() <= 0 || preferred.channelCount() <= 0
            || !m_inputResampler.configure(preferred.sampleRate(), preferred.channelCount(), preferred.sampleFormat(),
                                            AudioDecoder::kOutputSampleRate, AudioDecoder::kOutputChannels, QAudioFormat::Float)) {
            qWarning() << "AudioInputCapture: input device does not support float32 stereo 44.1kHz"
                       << "and has no usable preferred format";
            return false;
        }
        qInfo() << "AudioInputCapture: device provides" << preferred.sampleRate() << "Hz"
                << preferred.channelCount() << "ch, resampling to 44.1kHz stereo float";
        format = preferred;
        m_needsInputResample = true;
    }

    m_source = std::make_unique<QAudioSource>(device, format, this);
    m_inputIODevice = m_source->start();
    if (!m_inputIODevice) {
        qWarning() << "AudioInputCapture: unable to start audio source";
        m_source.reset();
        return false;
    }

    m_pullTimer.start();
    pullInput();
    return true;
}

void AudioInputCapture::stop() {
    m_pullTimer.stop();
    m_inputIODevice = nullptr;
    if (m_source) {
        m_source->stop();
        m_source.reset();
    }
    m_effectChain.reset();
    m_needsInputResample = false;
    m_inputResampler.reset();
    AudioInputMixRegistry::clearDevice(m_targetOutputDeviceId);
}

void AudioInputCapture::pullInput() {
    if (!m_source || !m_inputIODevice)
        return;

    while (m_source->bytesAvailable() > 0) {
        QByteArray chunk = m_inputIODevice->read(m_source->bytesAvailable());
        if (chunk.isEmpty())
            break;

        if (m_needsInputResample) {
            chunk = m_inputResampler.convert(chunk);
            if (chunk.isEmpty())
                continue;
        }

        QByteArray processed;
        if (m_effectChain.hasFilters()) {
            if (!m_effectChain.process(chunk, processed))
                break;
            if (processed.isEmpty())
                break;
            chunk = std::move(processed);
        }

        applyGain(chunk);
        AudioInputMixRegistry::appendPcm(m_targetOutputDeviceId, chunk);
        if (m_programRecordingTap)
            m_programRecordingTap(chunk);
    }
}

void AudioInputCapture::applyGain(QByteArray &pcmChunk) const {
    const float gain = m_muted ? 0.0f : static_cast<float>(m_volumePercent) / 100.0f;
    if (gain == 1.0f) return;

    auto *samples = reinterpret_cast<float *>(pcmChunk.data());
    const int sampleCount = pcmChunk.size() / static_cast<int>(sizeof(float));
    for (int i = 0; i < sampleCount; ++i)
        samples[i] *= gain;
}
