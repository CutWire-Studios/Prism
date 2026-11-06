#pragma once

#include <QImage>

#include "core/Deck.h"

/// CPU readback surface for the program compositor — implemented by Compositor,
/// or by test fakes so OutputHub can run headless without a GL context.
class ProgramFrameSource {
public:
    virtual ~ProgramFrameSource() = default;

    virtual QImage programFrame() const = 0;
    virtual QImage deckProgramFrame(Deck deck) const = 0;
    virtual void setProgramFrameConsumerCount(int count) = 0;
    virtual void setDeckFrameConsumerCount(int count) = 0;
    virtual void captureOutputFrameNow() = 0;
};
