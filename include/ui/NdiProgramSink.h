#pragma once

#include "ui/ProgramOutputSink.h"
#include <QImage>
#include <memory>

/// Sends the program mix as an NDI source (obs-ndi, vMix, another SwitchX, etc.).
class NdiProgramSink : public ProgramOutputSink {
public:
    NdiProgramSink();
    ~NdiProgramSink() override;

    QString name() const override;
    bool    isAvailable() const override;
    bool    isActive() const override;

    bool start(const QString &streamName = {}) override;
    void stop() override;
    void submitFrame(const QImage &frame) override;

    QString ndiName() const { return m_ndiName; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    bool     m_active = false;
    QString  m_ndiName;
    QImage   m_frameBuffer;
};
