#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include "ui/nodes/AudioEffects.h"

struct AVFilterGraph;
struct AVFilterContext;
struct AVFrame;

/// Real-time PCM processor: 44.1 kHz stereo float in/out via FFmpeg libavfilter.
class AudioEffectChain {
public:
    static constexpr int kSampleRate = 44100;
    static constexpr int kChannels = 2;
    static constexpr int kFrameSamples = 1024;

    AudioEffectChain();
    ~AudioEffectChain();

    void setEffects(const QVector<AudioEffectRef> &effects);
    const QVector<AudioEffectRef> &effects() const { return m_effects; }

    /// Drop buffered filter state (call on seek / graph change).
    void reset();

    /// Process one PCM chunk; may return partial output when filters buffer.
    bool process(const QByteArray &in, QByteArray &out);

    bool hasFilters() const { return !m_filterChain.isEmpty(); }

private:
    bool rebuildGraph();
    void freeGraph();
    bool drainSink(QByteArray &out);
    bool pushSamples(const float *samples, int nbSamples, QByteArray &out);

    QVector<AudioEffectRef> m_effects;
    QString m_filterChain;
    QByteArray m_inputBuffer;
    AVFilterGraph *m_graph = nullptr;
    AVFilterContext *m_src = nullptr;
    AVFilterContext *m_sink = nullptr;
    AVFrame *m_inFrame = nullptr;
    AVFrame *m_outFrame = nullptr;
    qint64 m_pts = 0;
};
