#include "core/media/AudioPlayer.h"
#include "core/media/AudioInputMixRegistry.h"
#include <QAudioFormat>
#include <QAudioSink>
#include <QAudio>
#include <QCoreApplication>
#include <QEvent>
#include <QMediaDevices>
#include <QIODevice>
#include <QDebug>
#include <algorithm>

AudioPlayer::AudioPlayer(QObject *parent)
    : QObject(parent) {
    m_pushTimer.setInterval(20);
    connect(&m_pushTimer, &QTimer::timeout, this, &AudioPlayer::pushAudio);
}

AudioPlayer::~AudioPlayer() {
    blockSignals(true);
    QCoreApplication::removePostedEvents(this, QEvent::MetaCall);
    stop();
}

bool AudioPlayer::start(const QString &filePath, double startTimeSeconds) {
    stop();

    if (!m_decoder.open(filePath)) return false;

    const double targetAudioTime = startTimeSeconds - (m_delayMs / 1000.0);
    if (targetAudioTime < 0.0) {
        m_silenceBytesPending = static_cast<qint64>(-targetAudioTime * AudioDecoder::kOutputSampleRate * AudioDecoder::kOutputChannels * sizeof(float));
        m_decoder.seek(0.0);
    } else {
        m_silenceBytesPending = 0;
        m_decoder.seek(targetAudioTime);
    }

    QAudioFormat format;
    format.setSampleRate(AudioDecoder::kOutputSampleRate);
    format.setChannelCount(AudioDecoder::kOutputChannels);
    format.setSampleFormat(QAudioFormat::Float);

    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (!m_deviceId.isEmpty()) {
        const auto outputs = QMediaDevices::audioOutputs();
        for (const QAudioDevice &dev : outputs) {
            if (QString::fromUtf8(dev.id()) == m_deviceId) {
                device = dev;
                break;
            }
        }
        // If the saved device is no longer present, fall back to the default.
    }
    if (!device.isFormatSupported(format)) {
        qWarning() << "AudioPlayer: output device does not support float32 stereo 44.1kHz";
        m_decoder.close();
        return false;
    }

    m_sink = std::make_unique<QAudioSink>(device, format, this);
    const int bytesPerSecond = AudioDecoder::kOutputSampleRate * AudioDecoder::kOutputChannels * static_cast<int>(sizeof(float));
    m_sink->setBufferSize((bytesPerSecond * 200) / 1000);
    m_outputDevice = m_sink->start();
    if (!m_outputDevice) {
        qWarning() << "AudioPlayer: unable to start audio sink";
        m_sink.reset();
        m_decoder.close();
        return false;
    }

    m_currentFilePath = filePath;
    m_residualBuffer.clear();
    m_pushTimer.start();
    pushAudio();
    return true;
}

void AudioPlayer::stop() {
    m_pushTimer.stop();
    m_outputDevice = nullptr;
    if (m_sink) {
        m_sink->stop();
        m_sink.reset();
    }
    m_currentFilePath.clear();
    m_decoder.close();
    m_residualBuffer.clear();
    m_effectChain.reset();
    m_silenceBytesPending = 0;
}

void AudioPlayer::pause() {
    if (m_sink) m_sink->suspend();
}

void AudioPlayer::resume() {
    if (m_sink) m_sink->resume();
}

bool AudioPlayer::seek(double seconds) {
    m_residualBuffer.clear();
    m_effectChain.reset();
    // Drop the audio already queued inside the sink — otherwise up to
    // bufferSize (~200 ms) of pre-seek sound plays out first and the track
    // stays audibly behind the video from then on.
    if (m_sink) {
        const bool wasSuspended = (m_sink->state() == QAudio::SuspendedState);
        m_sink->reset();
        m_outputDevice = m_sink->start();
        if (wasSuspended)
            m_sink->suspend();
    }
    const double targetAudioTime = seconds - (m_delayMs / 1000.0);
    if (targetAudioTime < 0.0) {
        m_silenceBytesPending = static_cast<qint64>(-targetAudioTime * AudioDecoder::kOutputSampleRate * AudioDecoder::kOutputChannels * sizeof(float));
        return m_decoder.seek(0.0);
    } else {
        m_silenceBytesPending = 0;
        return m_decoder.seek(targetAudioTime);
    }
}

void AudioPlayer::setVolumePercent(int volumePercent) {
    m_volumePercent = std::clamp(volumePercent, 0, 100);
}

void AudioPlayer::setMuted(bool muted) {
    m_muted = muted;
}

void AudioPlayer::setEffectChain(const QVector<AudioEffectRef> &effects) {
    m_effectChain.setEffects(effects);
}

bool AudioPlayer::isPlaying() const {
    return m_sink && (m_sink->state() == QAudio::ActiveState || m_sink->state() == QAudio::IdleState);
}

void AudioPlayer::pushAudio() {
    if (!m_sink || !m_outputDevice) return;

    auto writeFromBuffer = [&]() {
        if (m_residualBuffer.isEmpty()) return;
        const qint64 written = m_outputDevice->write(m_residualBuffer.constData(), m_residualBuffer.size());
        if (written > 0) {
            m_residualBuffer.remove(0, static_cast<int>(written));
        }
    };

    // 1. Write what we already have
    writeFromBuffer();

    // 2. Decode more if sink has space
    constexpr int bytesPerFrame = AudioDecoder::kOutputChannels * static_cast<int>(sizeof(float));
    
    while (m_sink && m_outputDevice
           && m_sink->bytesFree() >= bytesPerFrame
           && m_residualBuffer.size() < m_sink->bufferSize()) {
        QByteArray chunk;
        if (m_silenceBytesPending > 0) {
            const int sizeToPush = static_cast<int>(std::min<qint64>(m_silenceBytesPending, 4096));
            chunk.fill(0, sizeToPush);
            m_silenceBytesPending -= sizeToPush;
        } else {
            if (!m_decoder.decodeNextChunk(chunk)) {
                if (m_decoder.atEnd() && m_residualBuffer.isEmpty()) {
                    if (m_sink && m_sink->state() == QAudio::IdleState)
                        stop();
                }
                break;
            }

            QByteArray processed;
            if (m_effectChain.hasFilters()) {
                if (!m_effectChain.process(chunk, processed))
                    break;
                if (processed.isEmpty())
                    break;
                chunk = std::move(processed);
            }

            applyGain(chunk, 1.0f);
        }

        if (m_isoPcmTap)
            m_isoPcmTap(chunk);

        applyGain(chunk, m_crossfadeFactor);
        if (m_pcmTap)
            m_pcmTap(chunk);

        AudioInputMixRegistry::mixIntoPlaybackChunk(m_deviceId, chunk);

        if (!m_sink || !m_outputDevice)
            break;

        m_residualBuffer.append(chunk);
        writeFromBuffer();
    }
}

void AudioPlayer::applyGain(QByteArray &pcmChunk, float crossfadeFactor) const {
    const float gain = m_muted ? 0.0f
                               : static_cast<float>(m_volumePercent) / 100.0f * crossfadeFactor;
    if (gain == 1.0f) return;

    auto *samples = reinterpret_cast<float *>(pcmChunk.data());
    const int sampleCount = pcmChunk.size() / static_cast<int>(sizeof(float));
    for (int i = 0; i < sampleCount; ++i)
        samples[i] *= gain;
}
