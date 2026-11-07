#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>
#include <memory>
#include "core/graph/AudioEffects.h"

/// One open mic or loopback device shared by every scene that uses it. Level, mute and effects are
/// a single set applied for all consumers; PCM is fanned out to each consumer's tap.
class SharedAudioCapture {
public:
    using PcmFn = std::function<void(const QByteArray &)>;

    struct Settings {
        QString outputDeviceId;
        int volume = 100;
        bool muted = false;
        QVector<AudioEffectRef> effects;
    };

    class Backend {
    public:
        virtual ~Backend() = default;
        /// Applies the settings and returns whether the device must be (re)started.
        virtual bool configure(const QString &deviceId, const Settings &settings,
                               PcmFn program, PcmFn analysis) = 0;
        virtual void start() = 0;
        virtual void stop() = 0;
        virtual bool isRunning() const = 0;
    };

    static std::unique_ptr<SharedAudioCapture> input(PcmFn programSink);
    static std::unique_ptr<SharedAudioCapture> loopback(PcmFn programSink);

    SharedAudioCapture(std::unique_ptr<Backend> backend, PcmFn programSink);
    ~SharedAudioCapture();

    void setConsumers(QVector<PcmFn> consumers) { m_consumers = std::move(consumers); }
    void update(const QString &deviceId, const Settings &settings);
    void stop() { m_backend->stop(); }
    bool isRunning() const { return m_backend->isRunning(); }

private:
    void fanOut(const QByteArray &pcm) const;

    std::unique_ptr<Backend> m_backend;
    PcmFn m_programSink;
    QVector<PcmFn> m_consumers;
};
