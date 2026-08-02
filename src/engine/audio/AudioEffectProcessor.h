#pragma once

#include <QHash>
#include <QString>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <juce_dsp/juce_dsp.h>

namespace drift {

// Seconds a changed parameter takes to reach its new value. Long enough that a slider drag is a
// ramp rather than a step, short enough that the effect still feels responsive.
inline constexpr double kParamRampSeconds = 0.02;

// Jump a smoothed parameter straight to its target. The rack applies parameter values after
// prepare(), so without this a freshly built stage would spend its first 20 ms ramping up from a
// default nobody asked for.
inline void snapToTarget(juce::SmoothedValue<float> &value)
{
    value.setCurrentAndTargetValue(value.getTargetValue());
}

// One DSP stage. Deliberately not juce::AudioProcessor: that lives in juce_audio_processors, which
// depends on juce_gui_extra and would pull a GUI stack into a headless engine.
//
// The shape is also what a CLAP host adapter would implement, so plugin hosting could slot in
// behind the same factory later without re-architecting anything.
class AudioEffectProcessor
{
public:
    virtual ~AudioEffectProcessor() = default;

    virtual void prepare(const juce::dsp::ProcessSpec &spec) = 0;
    virtual void process(juce::dsp::AudioBlock<float> &block) = 0;
    virtual void reset() = 0;

    // Delay this stage adds. The rack primes by this much and discards, so a latent stage lines up
    // instead of drifting permanently late — the libavfilter path zero-filled the shortfall and
    // stayed offset forever.
    virtual int latencySamples() const { return 0; }
};

// One catalog effect: ordered stages plus a table binding manifest parameter identifiers to stage
// setters. Composition replaces the comma-joined avfilter chain string; the binding table replaces
// the {placeholder} text substitution.
//
// setParameter never rebuilds anything. The old graph tore itself down on every value change,
// which is exactly what made dragging a slider click.
class ChainProcessor final : public AudioEffectProcessor
{
public:
    template <typename T, typename... Args>
    T *addStage(Args &&...args)
    {
        auto stage = std::make_unique<T>(std::forward<Args>(args)...);
        T *raw = stage.get();
        m_stages.push_back(std::move(stage));
        return raw;
    }

    template <typename T>
    void bind(const QString &paramId, T *stage, void (T::*setter)(float))
    {
        m_bindings.insert(paramId, [stage, setter](float v) { (stage->*setter)(v); });
    }

    void bind(const QString &paramId, std::function<void(float)> setter);

    // Unknown ids are ignored: a project may carry a parameter an effect no longer has.
    void setParameter(const QString &paramId, float value);

    void prepare(const juce::dsp::ProcessSpec &spec) override;
    void process(juce::dsp::AudioBlock<float> &block) override;
    void reset() override;
    int latencySamples() const override;

private:
    std::vector<std::unique_ptr<AudioEffectProcessor>> m_stages;
    QHash<QString, std::function<void(float)>> m_bindings;
};

} // namespace drift
