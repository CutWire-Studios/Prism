#include "core/audio/dsp/FilterProcessors.h"

#include <cmath>

namespace prism {

namespace {

// Coefficient makers blow up at or above Nyquist; clamp well inside it.
float clampFrequency(float hz, double sampleRate)
{
    const float maximum = static_cast<float>(sampleRate * 0.45);
    return juce::jlimit(20.0f, maximum, hz);
}

} // namespace

// ---- ThreeBandEqProcessor ---------------------------------------------------------------

void ThreeBandEqProcessor::setLowGainDb(float db)
{
    m_lowDb = db;
    updateCoefficients();
}

void ThreeBandEqProcessor::setMidGainDb(float db)
{
    m_midDb = db;
    updateCoefficients();
}

void ThreeBandEqProcessor::setHighGainDb(float db)
{
    m_highDb = db;
    updateCoefficients();
}

void ThreeBandEqProcessor::updateCoefficients()
{
    *m_low.state = *juce::dsp::IIR::Coefficients<float>::makeLowShelf(
        m_sampleRate, clampFrequency(110.0f, m_sampleRate), 0.707f,
        juce::Decibels::decibelsToGain(m_lowDb));
    *m_mid.state = *juce::dsp::IIR::Coefficients<float>::makePeakFilter(
        m_sampleRate, clampFrequency(1000.0f, m_sampleRate), 1.2f,
        juce::Decibels::decibelsToGain(m_midDb));
    *m_high.state = *juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        m_sampleRate, clampFrequency(8000.0f, m_sampleRate), 0.707f,
        juce::Decibels::decibelsToGain(m_highDb));
}

void ThreeBandEqProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_low.prepare(spec);
    m_mid.prepare(spec);
    m_high.prepare(spec);
    updateCoefficients();
}

void ThreeBandEqProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    juce::dsp::ProcessContextReplacing<float> context(block);
    m_low.process(context);
    m_mid.process(context);
    m_high.process(context);
}

void ThreeBandEqProcessor::reset()
{
    m_low.reset();
    m_mid.reset();
    m_high.reset();
}

// ---- BandFilterProcessor ----------------------------------------------------------------

BandFilterProcessor::BandFilterProcessor(Mode mode, float frequencyHz)
    : m_mode(mode)
    , m_frequency(frequencyHz)
{
}

void BandFilterProcessor::setFrequency(float hz)
{
    m_frequency = hz;
    updateCoefficients();
}

void BandFilterProcessor::setWidthHz(float hz)
{
    m_widthHz = hz;
    updateCoefficients();
}

void BandFilterProcessor::updateCoefficients()
{
    const float frequency = clampFrequency(m_frequency, m_sampleRate);
    switch (m_mode) {
    case Mode::HighPass:
        *m_filter.state = *juce::dsp::IIR::Coefficients<float>::makeHighPass(m_sampleRate, frequency);
        break;
    case Mode::LowPass:
        *m_filter.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(m_sampleRate, frequency);
        break;
    case Mode::BandPass: {
        const float q = juce::jlimit(0.1f, 20.0f, frequency / juce::jmax(1.0f, m_widthHz));
        *m_filter.state =
            *juce::dsp::IIR::Coefficients<float>::makeBandPass(m_sampleRate, frequency, q);
        break;
    }
    }
}

void BandFilterProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_sampleRate = spec.sampleRate;
    m_filter.prepare(spec);
    updateCoefficients();
}

void BandFilterProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    juce::dsp::ProcessContextReplacing<float> context(block);
    m_filter.process(context);
}

void BandFilterProcessor::reset()
{
    m_filter.reset();
}

// ---- BitCrusherProcessor ----------------------------------------------------------------

BitCrusherProcessor::BitCrusherProcessor(Mode mode, float bits)
    : m_mode(mode)
    , m_bits(bits)
{
}

void BitCrusherProcessor::setBits(float bits)
{
    m_bits = juce::jlimit(1.0f, 24.0f, bits);
}

void BitCrusherProcessor::setSampleReduction(float samples)
{
    m_samples = juce::jlimit(1.0f, 250.0f, samples);
}

void BitCrusherProcessor::setMix(float mix)
{
    m_mix.setTargetValue(juce::jlimit(0.0f, 1.0f, mix));
}

float BitCrusherProcessor::crush(float x) const
{
    if (m_mode == Mode::Linear) {
        const float levels = std::pow(2.0f, m_bits - 1.0f);
        return std::round(x * levels) / levels;
    }

    // Logarithmic: quantisation steps scale with amplitude, so quiet detail is crushed as hard as
    // loud detail. Below the floor there is nothing meaningful left to quantise.
    const float magnitude = std::abs(x);
    if (magnitude < 1.0e-6f)
        return 0.0f;
    const float steps = juce::jmax(1.0f, m_bits * 4.0f);
    const float logMagnitude = std::log2(magnitude);
    const float quantised = std::round(logMagnitude * steps) / steps;
    return std::copysign(std::exp2(quantised), x);
}

void BitCrusherProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_mix.reset(spec.sampleRate, kParamRampSeconds);
    reset();
}

void BitCrusherProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());
    const int reduction = static_cast<int>(m_samples);

    for (int i = 0; i < frames; ++i) {
        const float mix = m_mix.getNextValue();
        for (int channel = 0; channel < channels; ++channel) {
            const float dry = block.getSample(channel, i);
            // Sample-and-hold ahead of the quantiser: `samples` input frames share one output.
            if (m_holdCount[channel] <= 0) {
                m_hold[channel] = crush(dry);
                m_holdCount[channel] = reduction;
            }
            --m_holdCount[channel];
            block.setSample(channel, i, dry + (m_hold[channel] - dry) * mix);
        }
    }
}

void BitCrusherProcessor::reset()
{
    snapToTarget(m_mix);
    for (int channel = 0; channel < 2; ++channel) {
        m_hold[channel] = 0.0f;
        m_holdCount[channel] = 0;
    }
}

// ---- CrystalizerProcessor ---------------------------------------------------------------

void CrystalizerProcessor::setIntensity(float intensity)
{
    m_intensity.setTargetValue(intensity);
}

void CrystalizerProcessor::setColors(float colors)
{
    m_colors = juce::jlimit(2.0f, 256.0f, colors);
}

void CrystalizerProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_intensity.reset(spec.sampleRate, kParamRampSeconds);
    reset();
}

void CrystalizerProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());

    for (int i = 0; i < frames; ++i) {
        const float intensity = m_intensity.getNextValue();
        const float step = 2.0f / m_colors;
        for (int channel = 0; channel < channels; ++channel) {
            const float in = block.getSample(channel, i);
            float out = in + (in - m_previous[channel]) * intensity;
            m_previous[channel] = in;
            out = std::round(out / step) * step;
            block.setSample(channel, i, juce::jlimit(-1.0f, 1.0f, out));
        }
    }
}

void CrystalizerProcessor::reset()
{
    snapToTarget(m_intensity);
    m_previous[0] = 0.0f;
    m_previous[1] = 0.0f;
}

// ---- GainProcessor ----------------------------------------------------------------------

GainProcessor::GainProcessor(float linearGain)
    : m_target(linearGain)
{
}

void GainProcessor::setGain(float linear)
{
    m_target = linear;
    m_gain.setTargetValue(linear);
}

void GainProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    m_gain.reset(spec.sampleRate, kParamRampSeconds);
    m_gain.setCurrentAndTargetValue(m_target);
}

void GainProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    const int channels = static_cast<int>(block.getNumChannels());
    const int frames = static_cast<int>(block.getNumSamples());

    for (int i = 0; i < frames; ++i) {
        const float gain = m_gain.getNextValue();
        for (int channel = 0; channel < channels; ++channel)
            block.setSample(channel, i, block.getSample(channel, i) * gain);
    }
}

void GainProcessor::reset()
{
    m_gain.setCurrentAndTargetValue(m_target);
}

} // namespace prism
