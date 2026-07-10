#include "core/media/AudioLoopbackCapture.h"
#include "core/media/AudioInputMixRegistry.h"
#include "ui/common/AudioLoopbackEnumerator.h"

#include <QAudioFormat>
#include <QAudioSource>
#include <QDebug>

#include <algorithm>

#if defined(Q_OS_LINUX)
#include "core/media/AudioLoopbackCaptureGStreamer.h"
#elif defined(Q_OS_WIN)
#include "core/media/AudioLoopbackCaptureWasapi.h"
#endif

AudioLoopbackCapture::AudioLoopbackCapture(QObject *parent)
    : QObject(parent)
{
    m_pullTimer.setInterval(20);
    connect(&m_pullTimer, &QTimer::timeout, this, [this]() { pullInput(); });
}

AudioLoopbackCapture::~AudioLoopbackCapture() {
    stop();
}

void AudioLoopbackCapture::setTargetOutputDeviceId(const QString &deviceId) {
    if (m_targetOutputDeviceId == deviceId)
        return;
    if (isRunning())
        AudioInputMixRegistry::clearDevice(m_targetOutputDeviceId);
    m_targetOutputDeviceId = deviceId;
}

void AudioLoopbackCapture::setVolumePercent(int volumePercent) {
    m_volumePercent = std::clamp(volumePercent, 0, 100);
}

void AudioLoopbackCapture::setEffectChain(const QVector<AudioEffectRef> &effects) {
    m_effectChain.setEffects(effects);
}

bool AudioLoopbackCapture::isRunning() const {
#if defined(Q_OS_LINUX)
    if (m_useGStreamer)
        return AudioLoopbackGst::isRunning();
#elif defined(Q_OS_WIN)
    if (m_useWasapi)
        return AudioLoopbackWasapi::isRunning();
#endif
    return m_source != nullptr;
}

bool AudioLoopbackCapture::start() {
    stop();

#if defined(Q_OS_WIN)
    // Windows has no PulseAudio-style ".monitor" input device — capture the
    // render endpoint directly via WASAPI loopback instead.
    m_useWasapi = false;
    if (AudioLoopbackWasapi::start(m_playbackDeviceId)) {
        m_useWasapi = true;
        m_pullTimer.start();
        pullInput();
        return true;
    }
    qWarning() << "AudioLoopbackCapture: WASAPI loopback capture failed for" << m_playbackDeviceId;
    return false;
#endif

    const QString monitorId =
        AudioLoopbackEnumerator::monitorSourceIdForPlayback(m_playbackDeviceId);
    if (monitorId.isEmpty()) {
        qWarning() << "AudioLoopbackCapture: no playback device for" << m_playbackDeviceId;
        return false;
    }

#if defined(Q_OS_LINUX)
    m_useGStreamer = false;
    if (AudioLoopbackGst::start(monitorId)) {
        m_useGStreamer = true;
        m_pullTimer.start();
        pullInput();
        return true;
    }
    qWarning() << "AudioLoopbackCapture: GStreamer monitor capture failed for" << monitorId
               << "- trying Qt audio input";
#endif

    const QAudioDevice device =
        AudioLoopbackEnumerator::monitorForPlaybackDevice(m_playbackDeviceId);
    if (device.isNull()) {
        qWarning() << "AudioLoopbackCapture: no monitor source for playback device"
                   << m_playbackDeviceId
                   << "(monitor id:" << monitorId << ")";
        return false;
    }

    if (!AudioLoopbackEnumerator::monitorSourceIdForPlayback(m_playbackDeviceId).isEmpty()
        && QString::fromUtf8(device.id()).startsWith(QStringLiteral("alsa_input."))) {
        qWarning() << "AudioLoopbackCapture: refusing microphone device" << device.description()
                   << "- monitor capture unavailable";
        return false;
    }

    QAudioFormat format;
    format.setSampleRate(AudioDecoder::kOutputSampleRate);
    format.setChannelCount(AudioDecoder::kOutputChannels);
    format.setSampleFormat(QAudioFormat::Float);

    qInfo() << "AudioLoopbackCapture: using Qt monitor" << device.description()
            << "for playback device" << m_playbackDeviceId;
    m_needsInputResample = false;
    if (!device.isFormatSupported(format)) {
        const QAudioFormat preferred = device.preferredFormat();
        if (!preferred.isValid() || preferred.sampleRate() <= 0 || preferred.channelCount() <= 0
            || !m_inputResampler.configure(preferred.sampleRate(), preferred.channelCount(), preferred.sampleFormat(),
                                            AudioDecoder::kOutputSampleRate, AudioDecoder::kOutputChannels, QAudioFormat::Float)) {
            qWarning() << "AudioLoopbackCapture: monitor device does not support float32 stereo 44.1kHz"
                       << "and has no usable preferred format:" << device.description();
            return false;
        }
        format = preferred;
        m_needsInputResample = true;
    }

    m_source = std::make_unique<QAudioSource>(device, format, this);
    m_inputIODevice = m_source->start();
    if (!m_inputIODevice) {
        qWarning() << "AudioLoopbackCapture: unable to start loopback capture on"
                   << device.description();
        m_source.reset();
        return false;
    }

    m_pullTimer.start();
    pullInput();
    return true;
}

void AudioLoopbackCapture::stop() {
    m_pullTimer.stop();
    m_inputIODevice = nullptr;
    if (m_source) {
        m_source->stop();
        m_source.reset();
    }
#if defined(Q_OS_LINUX)
    if (m_useGStreamer)
        AudioLoopbackGst::stop();
    m_useGStreamer = false;
#elif defined(Q_OS_WIN)
    if (m_useWasapi)
        AudioLoopbackWasapi::stop();
    m_useWasapi = false;
#endif
    m_effectChain.reset();
    m_needsInputResample = false;
    m_inputResampler.reset();
    AudioInputMixRegistry::clearDevice(m_targetOutputDeviceId);
}

void AudioLoopbackCapture::pullInput() {
    QByteArray chunk;

#if defined(Q_OS_LINUX)
    if (m_useGStreamer) {
        if (!AudioLoopbackGst::pull(chunk))
            return;
    } else
#elif defined(Q_OS_WIN)
    if (m_useWasapi) {
        if (!AudioLoopbackWasapi::pull(chunk))
            return;
    } else
#endif
    {
        if (!m_source || !m_inputIODevice)
            return;

        if (m_source->bytesAvailable() <= 0)
            return;

        chunk = m_inputIODevice->read(m_source->bytesAvailable());
        if (chunk.isEmpty())
            return;

        if (m_needsInputResample) {
            chunk = m_inputResampler.convert(chunk);
            if (chunk.isEmpty())
                return;
        }
    }

    QByteArray processed;
    if (m_effectChain.hasFilters()) {
        if (!m_effectChain.process(chunk, processed))
            return;
        if (processed.isEmpty())
            return;
        chunk = std::move(processed);
    }

    if (m_analysisTap)
        m_analysisTap(chunk);

    applyGain(chunk);
    AudioInputMixRegistry::appendPcm(m_targetOutputDeviceId, chunk);
    if (m_programRecordingTap)
        m_programRecordingTap(chunk);
}

void AudioLoopbackCapture::applyGain(QByteArray &pcmChunk) const {
    const float gain = m_muted ? 0.0f : static_cast<float>(m_volumePercent) / 100.0f;
    if (gain == 1.0f) return;

    auto *samples = reinterpret_cast<float *>(pcmChunk.data());
    const int sampleCount = pcmChunk.size() / static_cast<int>(sizeof(float));
    for (int i = 0; i < sampleCount; ++i)
        samples[i] *= gain;
}
