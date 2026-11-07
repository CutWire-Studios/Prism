#pragma once

#include <QObject>
#include <QVector>
#include <functional>

/// Global tempo clock: a beat position that advances at `bpm`, tap tempo and nudge.
/// GUI-thread only; the time source is injectable so tests stay deterministic.
class BpmClock : public QObject {
    Q_OBJECT
public:
    using TimeSource = std::function<qint64()>;   // milliseconds, monotonic

    static constexpr double kDefaultBpm = 120.0;
    static constexpr double kMinBpm = 20.0;
    static constexpr double kMaxBpm = 300.0;
    static constexpr int kTapWindow = 4;
    static constexpr qint64 kTapResetMs = 2000;
    static constexpr qint64 kNudgeMs = 250;
    static constexpr double kNudgeRate = 0.25;

    explicit BpmClock(QObject *parent = nullptr, TimeSource timeSource = {});

    double bpm() const { return m_bpm; }
    void setBpm(double bpm);

    /// Average of the last four taps; a gap over 2 s starts a new tap sequence. Also snaps the
    /// beat position to the nearest whole beat so the tap lands on the beat.
    void tap();
    /// Briefly speeds up (dir > 0) or slows down (dir < 0) the clock, shifting the phase.
    void nudge(int dir);

    /// Beat count plus phase, e.g. 12.25.
    double beats() const;
    int beatCount() const;
    double beatPhase() const;
    /// Phase within a 4/4 bar.
    double barPhase() const;

signals:
    void bpmChanged();

private:
    void advance() const;

    TimeSource m_time;
    double m_bpm = kDefaultBpm;
    mutable double m_beats = 0.0;
    mutable qint64 m_last = 0;
    qint64 m_nudgeUntil = 0;
    int m_nudgeDir = 0;
    QVector<qint64> m_taps;
};
