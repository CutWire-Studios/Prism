#pragma once

#include <QImage>
#include <QString>

/// Sends the program compositor feed to an external destination (NDI, Spout, …).
class ProgramOutputSink {
public:
    virtual ~ProgramOutputSink() = default;

    virtual QString name() const = 0;
    virtual bool    isAvailable() const = 0;
    virtual bool    isActive() const = 0;

    virtual bool start(const QString &streamName = {}) = 0;
    virtual void stop() = 0;
    virtual void submitFrame(const QImage &frame) = 0;
};
