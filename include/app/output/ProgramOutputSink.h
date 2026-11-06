#pragma once

#include <QImage>
#include <QString>

/// Strategy interface for sending the program compositor feed to an external
/// destination. OutputHub holds these polymorphically and calls submitFrame()
/// each tick for every active sink. Current implementors: NdiProgramSink,
/// VirtualCameraProgramSink. To add a new destination, implement this interface
/// and register an instance with the OutputHub — nothing else needs to change.
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
