#pragma once

#include <QImage>

/// CPU readback surface for the program compositor — implemented by VideoWidget,
/// or by test fakes so OutputHub can run headless without a GL context.
class ProgramFrameSource {
public:
    virtual ~ProgramFrameSource() = default;

    virtual QImage programFrame() const = 0;
    virtual QImage deckProgramFrame(bool deckA) const = 0;
    virtual void setProgramFrameConsumerCount(int count) = 0;
    virtual void setDeckFrameConsumerCount(int count) = 0;
    virtual void captureOutputFrameNow() = 0;
};
