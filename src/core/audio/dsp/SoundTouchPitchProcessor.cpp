#include "core/audio/dsp/SoundTouchPitchProcessor.h"

#include <soundtouch/SoundTouch.h>

#include <algorithm>
#include <cstring>

namespace prism {

namespace {

constexpr int kChannels = 2;
// The rack never hands a stage more than this at once.
constexpr int kMaxBlock = 1024;

} // namespace

SoundTouchPitchProcessor::SoundTouchPitchProcessor()
    : m_shifter(std::make_unique<soundtouch::SoundTouch>())
{
}

SoundTouchPitchProcessor::~SoundTouchPitchProcessor() = default;

void SoundTouchPitchProcessor::setRatio(float ratio)
{
    const float clamped = juce::jlimit(0.25f, 4.0f, ratio);
    if (juce::approximatelyEqual(clamped, m_ratio))
        return;
    m_ratio = clamped;
    applyRatio();
}

void SoundTouchPitchProcessor::applyRatio()
{
    if (m_sampleRate <= 0.0)
        return;

    // Pitch only: tempo and rate stay at 1.0 so the clip keeps its length.
    m_shifter->setPitch(m_ratio);

    // How much input SoundTouch needs before the first batch comes out, plus one nominal output
    // batch of slack. The slack is what keeps burst timing from momentarily emptying the FIFO
    // mid-stream, which would punch a hole in the audio exactly like the dropouts we just removed.
    const int initial = m_shifter->getSetting(SETTING_INITIAL_LATENCY);
    const int nominal = m_shifter->getSetting(SETTING_NOMINAL_OUTPUT_SEQUENCE);
    m_reservoir = std::max(64, nominal);
    m_latency = std::max(0, initial) + m_reservoir;

    const size_t capacity = static_cast<size_t>(m_latency + nominal + kMaxBlock * 2) * kChannels;
    if (m_fifo.capacity() < capacity)
        m_fifo.reserve(capacity);
}

void SoundTouchPitchProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;

    m_shifter->setSampleRate(static_cast<uint>(spec.sampleRate));
    m_shifter->setChannels(kChannels);
    m_shifter->setTempo(1.0);
    m_shifter->setRate(1.0);
    // Quickseek trades correlation accuracy for speed and is audible on voice; the anti-alias
    // filter matters because pitch shifting resamples.
    m_shifter->setSetting(SETTING_USE_QUICKSEEK, 0);
    m_shifter->setSetting(SETTING_USE_AA_FILTER, 1);

    m_interleaved.assign(static_cast<size_t>(kMaxBlock) * kChannels, 0.0f);
    applyRatio();
    reset();
}

void SoundTouchPitchProcessor::drainIntoFifo()
{
    // receiveSamples takes frames and returns frames.
    for (;;) {
        const size_t used = m_fifo.size();
        m_fifo.resize(used + static_cast<size_t>(kMaxBlock) * kChannels);
        const uint got = m_shifter->receiveSamples(m_fifo.data() + used, kMaxBlock);
        m_fifo.resize(used + static_cast<size_t>(got) * kChannels);
        if (got == 0)
            break;
    }
}

void SoundTouchPitchProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int frames = static_cast<int>(block.getNumSamples());
    const int channels = static_cast<int>(block.getNumChannels());
    if (frames <= 0 || channels < 1)
        return;

    for (int i = 0; i < frames; ++i) {
        const float left = block.getSample(0, i);
        m_interleaved[static_cast<size_t>(i) * 2] = left;
        m_interleaved[static_cast<size_t>(i) * 2 + 1] = channels > 1 ? block.getSample(1, i) : left;
    }

    m_shifter->putSamples(m_interleaved.data(), static_cast<uint>(frames));
    drainIntoFifo();

    const size_t available = (m_fifo.size() - m_fifoRead) / kChannels;

    // Hold output back until the reservoir has built. Those held samples are this stage's latency,
    // and the rack primes them away with real audio before the first block the user hears.
    if (m_filling) {
        if (available < static_cast<size_t>(m_reservoir + frames)) {
            block.clear();
            return;
        }
        m_filling = false;
    }

    const int emitFrames = static_cast<int>(std::min<size_t>(available, static_cast<size_t>(frames)));
    for (int i = 0; i < emitFrames; ++i) {
        const size_t base = m_fifoRead + static_cast<size_t>(i) * kChannels;
        block.setSample(0, i, m_fifo[base]);
        if (channels > 1)
            block.setSample(1, i, m_fifo[base + 1]);
    }
    // Only reachable if the reservoir was exhausted despite the slack above.
    for (int i = emitFrames; i < frames; ++i) {
        block.setSample(0, i, 0.0f);
        if (channels > 1)
            block.setSample(1, i, 0.0f);
    }

    m_fifoRead += static_cast<size_t>(emitFrames) * kChannels;
    if (m_fifoRead > 0 && m_fifoRead >= m_fifo.size()) {
        m_fifo.clear();
        m_fifoRead = 0;
    } else if (m_fifoRead >= static_cast<size_t>(kMaxBlock) * kChannels * 4) {
        m_fifo.erase(m_fifo.begin(), m_fifo.begin() + static_cast<long>(m_fifoRead));
        m_fifoRead = 0;
    }
}

void SoundTouchPitchProcessor::reset()
{
    m_shifter->clear();
    m_fifo.clear();
    m_fifoRead = 0;
    m_filling = true;
}

} // namespace prism
