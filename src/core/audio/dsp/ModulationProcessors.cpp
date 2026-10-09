#include "core/audio/dsp/ModulationProcessors.h"

#include <cmath>

namespace prism {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Advance an LFO phase kept in turns rather than radians, so wrapping is exact.
void advancePhase(double &phase, float rateHz, double sampleRate)
{
    phase += static_cast<double>(rateHz) / sampleRate;
    if (phase >= 1.0)
        phase -= std::floor(phase);
}

// 0..1 triangle-free sine, offset a quarter turn so it starts at its maximum.
float unipolarSine(double phase, double offsetTurns = 0.25)
{
    return 0.5f * (1.0f + static_cast<float>(std::sin(kTwoPi * (phase + offsetTurns))));
}

int delayLineLengthFor(double sampleRate, double maxSeconds)
{
    return juce::jmax(64, static_cast<int>(sampleRate * maxSeconds) + 4);
}

} // namespace

// ---- TremoloProcessor -------------------------------------------------------------------

void TremoloProcessor::setRate(float hz)
{
    m_rate = juce::jlimit(0.01f, 20000.0f, hz);
}

void TremoloProcessor::setDepth(float depth)
{
    m_depth.setTargetValue(juce::jlimit(0.0f, 1.0f, depth));
}

void TremoloProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_depth.reset(spec.sampleRate, kParamRampSeconds);
    reset();
}

void TremoloProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());

    for (int i = 0; i < frames; ++i) {
        const float depth = m_depth.getNextValue();
        const float gain = 1.0f - depth + depth * unipolarSine(m_phase);
        advancePhase(m_phase, m_rate, m_sampleRate);
        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * gain);
    }
}

void TremoloProcessor::reset()
{
    snapToTarget(m_depth);
    m_phase = 0.0;
}

// ---- VibratoProcessor -------------------------------------------------------------------

void VibratoProcessor::setRate(float hz)
{
    m_rate = juce::jlimit(0.01f, 100.0f, hz);
}

void VibratoProcessor::setDepth(float depth)
{
    m_depth.setTargetValue(juce::jlimit(0.0f, 1.0f, depth));
}

void VibratoProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_delay.setMaximumDelayInSamples(delayLineLengthFor(spec.sampleRate, 0.010));
    m_delay.prepare(spec);
    m_depth.reset(spec.sampleRate, kParamRampSeconds);
    reset();
}

void VibratoProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());
    // Sweep around a 2.5 ms centre; depth 1.0 swings the full +/- 2.5 ms.
    const float centre = static_cast<float>(0.0025 * m_sampleRate);

    for (int i = 0; i < frames; ++i) {
        const float depth = m_depth.getNextValue();
        const float sweep = centre * depth * (unipolarSine(m_phase, 0.0) * 2.0f - 1.0f);
        const float delaySamples = juce::jmax(1.0f, centre + sweep);
        advancePhase(m_phase, m_rate, m_sampleRate);

        for (int channel = 0; channel < channels; ++channel) {
            m_delay.pushSample(channel, block.getSample(channel, i));
            block.setSample(channel, i, m_delay.popSample(channel, delaySamples));
        }
    }
}

void VibratoProcessor::reset()
{
    snapToTarget(m_depth);
    m_delay.reset();
    m_phase = 0.0;
}

// ---- FlangerProcessor -------------------------------------------------------------------

void FlangerProcessor::setDelayMs(float ms)
{
    m_delayMs = juce::jlimit(0.0f, 50.0f, ms);
}

void FlangerProcessor::setDepthMs(float ms)
{
    m_depthMs = juce::jlimit(0.0f, 50.0f, ms);
}

void FlangerProcessor::setRegenPercent(float percent)
{
    m_regen = juce::jlimit(-95.0f, 95.0f, percent);
}

void FlangerProcessor::setWidthPercent(float percent)
{
    m_width = juce::jlimit(0.0f, 100.0f, percent);
}

void FlangerProcessor::setRate(float hz)
{
    m_rate = juce::jlimit(0.01f, 20.0f, hz);
}

void FlangerProcessor::setPhaseDegrees(float degrees)
{
    m_phaseOffset = degrees / 360.0f;
}

void FlangerProcessor::setInvertRight(float enabled)
{
    m_invertRight = enabled >= 0.5f;
}

void FlangerProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_delay.setMaximumDelayInSamples(delayLineLengthFor(spec.sampleRate, 0.120));
    m_delay.prepare(spec);
    reset();
}

void FlangerProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());
    const float msToSamples = static_cast<float>(m_sampleRate / 1000.0);
    const float regen = m_regen / 100.0f;
    const float width = m_width / 100.0f;
    // Normalise so a wide mix does not simply get louder than a narrow one.
    const float normalise = 1.0f / (1.0f + width);

    for (int i = 0; i < frames; ++i) {
        for (int channel = 0; channel < channels; ++channel) {
            // Each channel sweeps at its own phase; that offset is the stereo movement.
            const double offset = channel == 0 ? 0.0 : static_cast<double>(m_phaseOffset);
            const float sweep = m_depthMs * unipolarSine(m_phase + offset, 0.0);
            const float delaySamples = juce::jmax(1.0f, (m_delayMs + sweep) * msToSamples);

            const float dry = block.getSample(channel, i);
            const float wet = m_delay.popSample(channel, delaySamples);
            m_delay.pushSample(channel, dry + wet * regen);

            float out = (dry + wet * width) * normalise;
            if (m_invertRight && channel == 1)
                out = -out;
            block.setSample(channel, i, out);
        }
        advancePhase(m_phase, m_rate, m_sampleRate);
    }
}

void FlangerProcessor::reset()
{
    m_delay.reset();
    m_phase = 0.0;
}

// ---- ChorusProcessor --------------------------------------------------------------------

void ChorusProcessor::setInGain(float linear)
{
    m_inTarget = juce::jmax(0.0f, linear);
    m_inGain.setTargetValue(m_inTarget);
}

void ChorusProcessor::setOutGain(float linear)
{
    m_outTarget = juce::jmax(0.0f, linear);
    m_outGain.setTargetValue(m_outTarget);
}

void ChorusProcessor::setDelayMs(float ms)
{
    // juce asserts strictly below 100 ms.
    m_chorus.setCentreDelay(juce::jlimit(1.0f, 99.0f, ms));
}

void ChorusProcessor::setDecay(float decay)
{
    m_chorus.setMix(juce::jlimit(0.0f, 1.0f, decay));
}

void ChorusProcessor::setRate(float hz)
{
    m_chorus.setRate(juce::jlimit(0.01f, 99.0f, hz));
}

void ChorusProcessor::setDepthMs(float ms)
{
    // avfilter states depth in milliseconds over a 0-10 range; juce wants it normalised.
    m_chorus.setDepth(juce::jlimit(0.0f, 1.0f, ms / 10.0f));
}

void ChorusProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_chorus.prepare(spec);
    m_chorus.setFeedback(0.0f);
    m_inGain.reset(spec.sampleRate, kParamRampSeconds);
    m_outGain.reset(spec.sampleRate, kParamRampSeconds);
    m_inGain.setCurrentAndTargetValue(m_inTarget);
    m_outGain.setCurrentAndTargetValue(m_outTarget);
}

void ChorusProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());

    for (int i = 0; i < frames; ++i) {
        const float gain = m_inGain.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * gain);
    }

    juce::dsp::ProcessContextReplacing<float> context(block);
    m_chorus.process(context);

    for (int i = 0; i < frames; ++i) {
        const float gain = m_outGain.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * gain);
    }
}

void ChorusProcessor::reset()
{
    m_chorus.reset();
    m_inGain.setCurrentAndTargetValue(m_inTarget);
    m_outGain.setCurrentAndTargetValue(m_outTarget);
}

// ---- PhaserProcessor --------------------------------------------------------------------

void PhaserProcessor::setInGain(float linear)
{
    m_inTarget = juce::jmax(0.0f, linear);
    m_inGain.setTargetValue(m_inTarget);
}

void PhaserProcessor::setOutGain(float linear)
{
    m_outTarget = juce::jmax(0.0f, linear);
    m_outGain.setTargetValue(m_outTarget);
}

void PhaserProcessor::setDelayMs(float ms)
{
    // 0-5 ms of all-pass spacing maps onto 1500 Hz down to 200 Hz of notch centre.
    const float centre = 1500.0f - juce::jlimit(0.0f, 5.0f, ms) * 260.0f;
    m_phaser.setCentreFrequency(
        juce::jlimit(20.0f, static_cast<float>(m_sampleRate * 0.45), centre));
}

void PhaserProcessor::setDecay(float decay)
{
    m_phaser.setFeedback(juce::jlimit(0.0f, 0.95f, decay));
}

void PhaserProcessor::setRate(float hz)
{
    m_phaser.setRate(juce::jlimit(0.01f, 99.0f, hz));
}

void PhaserProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_phaser.prepare(spec);
    m_phaser.setDepth(1.0f);
    m_phaser.setMix(0.5f);
    m_inGain.reset(spec.sampleRate, kParamRampSeconds);
    m_outGain.reset(spec.sampleRate, kParamRampSeconds);
    m_inGain.setCurrentAndTargetValue(m_inTarget);
    m_outGain.setCurrentAndTargetValue(m_outTarget);
}

void PhaserProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());

    for (int i = 0; i < frames; ++i) {
        const float gain = m_inGain.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * gain);
    }

    juce::dsp::ProcessContextReplacing<float> context(block);
    m_phaser.process(context);

    for (int i = 0; i < frames; ++i) {
        const float gain = m_outGain.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * gain);
    }
}

void PhaserProcessor::reset()
{
    m_phaser.reset();
    m_inGain.setCurrentAndTargetValue(m_inTarget);
    m_outGain.setCurrentAndTargetValue(m_outTarget);
}

// ---- AutoPanProcessor -------------------------------------------------------------------

void AutoPanProcessor::setRate(float hz)
{
    m_rate = juce::jlimit(0.001f, 100.0f, hz);
}

void AutoPanProcessor::setAmount(float amount)
{
    m_amount.setTargetValue(juce::jlimit(0.0f, 1.0f, amount));
}

void AutoPanProcessor::setLevelIn(float linear)
{
    m_levelInTarget = juce::jmax(0.0f, linear);
    m_levelIn.setTargetValue(m_levelInTarget);
}

void AutoPanProcessor::setLevelOut(float linear)
{
    m_levelOutTarget = juce::jmax(0.0f, linear);
    m_levelOut.setTargetValue(m_levelOutTarget);
}

void AutoPanProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_amount.reset(spec.sampleRate, kParamRampSeconds);
    m_levelIn.reset(spec.sampleRate, kParamRampSeconds);
    m_levelOut.reset(spec.sampleRate, kParamRampSeconds);
    m_levelIn.setCurrentAndTargetValue(m_levelInTarget);
    m_levelOut.setCurrentAndTargetValue(m_levelOutTarget);
    reset();
}

void AutoPanProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());

    for (int i = 0; i < frames; ++i) {
        const float amount = m_amount.getNextValue();
        const float level = m_levelIn.getNextValue() * m_levelOut.getNextValue();

        for (int channel = 0; channel < channels; ++channel) {
            // offset_l=0, offset_r=0.5: half a cycle apart.
            const double offset = channel == 0 ? 0.0 : 0.5;
            const float lfo = unipolarSine(m_phase + offset, 0.0);
            const float gain = (1.0f - amount + amount * lfo) * level;
            block.setSample(channel, i, block.getSample(channel, i) * gain);
        }
        advancePhase(m_phase, m_rate, m_sampleRate);
    }
}

void AutoPanProcessor::reset()
{
    snapToTarget(m_amount);
    snapToTarget(m_levelIn);
    snapToTarget(m_levelOut);
    m_phase = 0.0;
}

// ---- StereoWidenProcessor ---------------------------------------------------------------

void StereoWidenProcessor::setDelayMs(float ms)
{
    m_delayMs = juce::jlimit(1.0f, 100.0f, ms);
}

void StereoWidenProcessor::setFeedback(float feedback)
{
    m_feedback.setTargetValue(juce::jlimit(0.0f, 0.9f, feedback));
}

void StereoWidenProcessor::setCrossfeed(float crossfeed)
{
    m_crossfeed.setTargetValue(juce::jlimit(0.0f, 0.8f, crossfeed));
}

void StereoWidenProcessor::setDryMix(float dryMix)
{
    m_dryMix.setTargetValue(juce::jlimit(0.0f, 1.0f, dryMix));
}

void StereoWidenProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_delay.setMaximumDelayInSamples(delayLineLengthFor(spec.sampleRate, 0.120));
    m_delay.prepare(spec);
    m_feedback.reset(spec.sampleRate, kParamRampSeconds);
    m_crossfeed.reset(spec.sampleRate, kParamRampSeconds);
    m_dryMix.reset(spec.sampleRate, kParamRampSeconds);
    reset();
}

void StereoWidenProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    if (block.getNumChannels() < 2)
        return;

    const int frames = static_cast<int>(block.getNumSamples());
    const float delaySamples =
        juce::jmax(1.0f, m_delayMs * static_cast<float>(m_sampleRate / 1000.0));

    for (int i = 0; i < frames; ++i) {
        const float feedback = m_feedback.getNextValue();
        const float crossfeed = m_crossfeed.getNextValue();
        const float dryMix = m_dryMix.getNextValue();

        const float left = block.getSample(0, i);
        const float right = block.getSample(1, i);
        const float delayedLeft = m_delay.popSample(0, delaySamples);
        const float delayedRight = m_delay.popSample(1, delaySamples);

        m_delay.pushSample(0, left + delayedRight * feedback);
        m_delay.pushSample(1, right + delayedLeft * feedback);

        // Subtracting the opposite channel's delayed copy is what pushes the image outward.
        block.setSample(0, i, left * dryMix - delayedRight * crossfeed);
        block.setSample(1, i, right * dryMix - delayedLeft * crossfeed);
    }
}

void StereoWidenProcessor::reset()
{
    snapToTarget(m_feedback);
    snapToTarget(m_crossfeed);
    snapToTarget(m_dryMix);
    m_delay.reset();
}

// ---- EchoProcessor ----------------------------------------------------------------------

void EchoProcessor::setInGain(float linear)
{
    m_inTarget = juce::jmax(0.0f, linear);
    m_inGain.setTargetValue(m_inTarget);
}

void EchoProcessor::setOutGain(float linear)
{
    m_outTarget = juce::jmax(0.0f, linear);
    m_outGain.setTargetValue(m_outTarget);
}

void EchoProcessor::setDelayMs(float ms)
{
    m_delayMs = juce::jlimit(1.0f, 1000.0f, ms);
}

void EchoProcessor::setDecay(float decay)
{
    m_decay.setTargetValue(juce::jlimit(0.0f, 1.0f, decay));
}

void EchoProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_delay.setMaximumDelayInSamples(delayLineLengthFor(spec.sampleRate, 1.05));
    m_delay.prepare(spec);
    m_inGain.reset(spec.sampleRate, kParamRampSeconds);
    m_outGain.reset(spec.sampleRate, kParamRampSeconds);
    m_decay.reset(spec.sampleRate, kParamRampSeconds);
    m_inGain.setCurrentAndTargetValue(m_inTarget);
    m_outGain.setCurrentAndTargetValue(m_outTarget);
    reset();
}

void EchoProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());
    const float delaySamples =
        juce::jmax(1.0f, m_delayMs * static_cast<float>(m_sampleRate / 1000.0));

    for (int i = 0; i < frames; ++i) {
        const float inGain = m_inGain.getNextValue();
        const float outGain = m_outGain.getNextValue();
        const float decay = m_decay.getNextValue();

        for (int channel = 0; channel < channels; ++channel) {
            const float dry = block.getSample(channel, i) * inGain;
            const float delayed = m_delay.popSample(channel, delaySamples);
            // The line carries the input, not the output: one repeat, no regeneration.
            m_delay.pushSample(channel, dry);
            block.setSample(channel, i, (dry + delayed * decay) * outGain);
        }
    }
}

void EchoProcessor::reset()
{
    snapToTarget(m_decay);
    m_delay.reset();
    m_inGain.setCurrentAndTargetValue(m_inTarget);
    m_outGain.setCurrentAndTargetValue(m_outTarget);
}

} // namespace prism
