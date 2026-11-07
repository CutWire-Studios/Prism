#include "app/BpmClock.h"

#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <memory>

BpmClock::BpmClock(QObject *parent, TimeSource timeSource)
    : QObject(parent), m_time(std::move(timeSource))
{
    if (!m_time) {
        auto timer = std::make_shared<QElapsedTimer>();
        timer->start();
        m_time = [timer] { return timer->elapsed(); };
    }
    m_last = m_time();
}

void BpmClock::advance() const
{
    const qint64 now = m_time();
    const qint64 dtMs = now - m_last;
    if (dtMs <= 0)
        return;
    const qint64 nudged = std::clamp<qint64>(std::min(now, m_nudgeUntil) - m_last, 0, dtMs);
    const double seconds = (double(dtMs - nudged) + double(nudged) * (1.0 + kNudgeRate * m_nudgeDir)) * 0.001;
    m_beats += seconds * m_bpm / 60.0;
    m_last = now;
}

void BpmClock::setBpm(double bpm)
{
    bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
    if (bpm == m_bpm)
        return;
    advance();
    m_bpm = bpm;
    emit bpmChanged();
}

void BpmClock::tap()
{
    const qint64 now = m_time();
    if (!m_taps.isEmpty() && now - m_taps.last() > kTapResetMs)
        m_taps.clear();
    m_taps.append(now);
    while (m_taps.size() > kTapWindow)
        m_taps.removeFirst();

    advance();
    m_beats = std::round(m_beats);

    if (m_taps.size() >= 2) {
        const double meanMs = double(m_taps.last() - m_taps.first()) / double(m_taps.size() - 1);
        if (meanMs > 0.0)
            setBpm(60000.0 / meanMs);
    }
}

void BpmClock::nudge(int dir)
{
    advance();
    m_nudgeDir = dir > 0 ? 1 : dir < 0 ? -1 : 0;
    m_nudgeUntil = m_last + kNudgeMs;
}

double BpmClock::beats() const
{
    advance();
    return m_beats;
}

int BpmClock::beatCount() const { return int(std::floor(beats())); }

double BpmClock::beatPhase() const
{
    const double b = beats();
    return b - std::floor(b);
}

double BpmClock::barPhase() const
{
    const double bar = beats() / 4.0;
    return bar - std::floor(bar);
}
