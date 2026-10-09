#pragma once

#include <cstdint>

namespace prism {

using TimeUs = int64_t;

constexpr TimeUs kUsPerSecond = 1'000'000;
constexpr TimeUs kUsPerMs = 1'000;

constexpr TimeUs secondsToUs(double seconds)
{
    return static_cast<TimeUs>(seconds * static_cast<double>(kUsPerSecond));
}

constexpr double usToSeconds(TimeUs us)
{
    return static_cast<double>(us) / static_cast<double>(kUsPerSecond);
}

constexpr TimeUs frameDurationUs(int fps)
{
    return fps > 0 ? kUsPerSecond / fps : kUsPerSecond / 30;
}

} // namespace prism
