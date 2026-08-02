#include "engine/audio/AudioEffectProcessor.h"

namespace drift {

void ChainProcessor::bind(const QString &paramId, std::function<void(float)> setter)
{
    m_bindings.insert(paramId, std::move(setter));
}

void ChainProcessor::setParameter(const QString &paramId, float value)
{
    const auto it = m_bindings.constFind(paramId);
    if (it != m_bindings.constEnd())
        it.value()(value);
}

void ChainProcessor::prepare(const juce::dsp::ProcessSpec &spec)
{
    for (auto &stage : m_stages)
        stage->prepare(spec);
}

void ChainProcessor::process(juce::dsp::AudioBlock<float> &block)
{
    for (auto &stage : m_stages)
        stage->process(block);
}

void ChainProcessor::reset()
{
    for (auto &stage : m_stages)
        stage->reset();
}

int ChainProcessor::latencySamples() const
{
    int total = 0;
    for (const auto &stage : m_stages)
        total += stage->latencySamples();
    return total;
}

} // namespace drift
