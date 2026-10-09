#pragma once

#include "core/audio/dsp/AudioEffectProcessor.h"
#include "core/audio/dsp/WavReader.h"

#include <memory>
#include <string_view>

namespace prism::audiofx {

// The chain behind one pedal type, with every knob in its PedalSpec bound by id. classic.* types
// come from the factory; the rest are juce::dsp building blocks defined in GraphPedals.cpp.
// `ir` is required by "convolution" and ignored otherwise. Null for an unknown type.
std::unique_ptr<ChainProcessor> createPedal(std::string_view type, std::shared_ptr<const IrData> ir = {});

} // namespace prism::audiofx
