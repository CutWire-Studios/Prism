#include "core/media/SharedAudioCapture.h"
#include "core/media/AudioInputCapture.h"
#include "core/media/AudioLoopbackCapture.h"

namespace {

template <class Capture>
void applySettings(Capture &c, const SharedAudioCapture::Settings &s) {
    c.setTargetOutputDeviceId(s.outputDeviceId);
    c.setVolumePercent(s.volume);
    c.setMuted(s.muted);
    c.setEffectChain(s.effects);
}

class InputBackend : public SharedAudioCapture::Backend {
public:
        bool configure(const QString &deviceId, const SharedAudioCapture::Settings &s,
                   SharedAudioCapture::PcmFn program, SharedAudioCapture::PcmFn analysis) override {
        const bool restart = !m_capture.isRunning()
            || m_capture.inputDeviceId() != deviceId
            || m_capture.targetOutputDeviceId() != s.outputDeviceId;
        m_capture.setInputDeviceId(deviceId);
        applySettings(m_capture, s);
        m_capture.setProgramRecordingTap([program, analysis](const QByteArray &pcm) {
            program(pcm);
            analysis(pcm);
        });
        return restart;
    }
    void start() override { m_capture.start(); }
    void stop() override { m_capture.stop(); }
    bool isRunning() const override { return m_capture.isRunning(); }

private:
    AudioInputCapture m_capture;
};

class LoopbackBackend : public SharedAudioCapture::Backend {
public:
        bool configure(const QString &deviceId, const SharedAudioCapture::Settings &s,
                   SharedAudioCapture::PcmFn program, SharedAudioCapture::PcmFn analysis) override {
        const bool restart = !m_capture.isRunning()
            || m_capture.playbackDeviceId() != deviceId
            || m_capture.targetOutputDeviceId() != s.outputDeviceId;
        m_capture.setPlaybackDeviceId(deviceId);
        applySettings(m_capture, s);
        m_capture.setProgramRecordingTap(std::move(program));
        m_capture.setAnalysisTap(std::move(analysis));
        return restart;
    }
    void start() override { m_capture.start(); }
    void stop() override { m_capture.stop(); }
    bool isRunning() const override { return m_capture.isRunning(); }

private:
    AudioLoopbackCapture m_capture;
};

}

std::unique_ptr<SharedAudioCapture> SharedAudioCapture::input(PcmFn programSink) {
    return std::make_unique<SharedAudioCapture>(std::make_unique<InputBackend>(), std::move(programSink));
}

std::unique_ptr<SharedAudioCapture> SharedAudioCapture::loopback(PcmFn programSink) {
    return std::make_unique<SharedAudioCapture>(std::make_unique<LoopbackBackend>(), std::move(programSink));
}

SharedAudioCapture::SharedAudioCapture(std::unique_ptr<Backend> backend, PcmFn programSink)
    : m_backend(std::move(backend))
    , m_programSink(std::move(programSink))
{
}

SharedAudioCapture::~SharedAudioCapture() = default;

void SharedAudioCapture::update(const QString &deviceId, const Settings &settings) {
    const bool restart = m_backend->configure(
        deviceId, settings,
        [this](const QByteArray &pcm) { if (m_programSink) m_programSink(pcm); },
        [this](const QByteArray &pcm) { fanOut(pcm); });
    if (restart)
        m_backend->start();
}

void SharedAudioCapture::fanOut(const QByteArray &pcm) const {
    for (const PcmFn &tap : m_consumers)
        tap(pcm);
}
