#include "engine/audio/AudioEffectFactory.h"

#include "engine/audio/DynamicsProcessors.h"
#include "engine/audio/FilterProcessors.h"
#include "engine/audio/ModulationProcessors.h"
#include "engine/audio/SoundTouchPitchProcessor.h"

#include <QHash>

#include <cmath>
#include <functional>

namespace drift::audiofx {

namespace {

using Builder = std::function<void(ChainProcessor &)>;

// Manifests whose parameter is a linear amplitude where the stage wants dB. The avfilter chains
// were inconsistent about this — utility.compressor wrote "{threshold}dB" while texture.tape wrote
// a bare linear threshold — and saved projects store whichever the manifest used, so the
// conversion belongs here rather than in a rewritten manifest.
float linearToDb(float linear)
{
    return 20.0f * std::log10(std::max(linear, 1.0e-5f));
}

// ---- utility ----------------------------------------------------------------------------

void buildEq3(ChainProcessor &chain)
{
    auto *eq = chain.addStage<ThreeBandEqProcessor>();
    chain.bind(QStringLiteral("low"), eq, &ThreeBandEqProcessor::setLowGainDb);
    chain.bind(QStringLiteral("mid"), eq, &ThreeBandEqProcessor::setMidGainDb);
    chain.bind(QStringLiteral("high"), eq, &ThreeBandEqProcessor::setHighGainDb);
}

void buildCompressor(ChainProcessor &chain)
{
    auto *compressor = chain.addStage<CompressorProcessor>();
    chain.bind(QStringLiteral("threshold"), compressor, &CompressorProcessor::setThresholdDb);
    chain.bind(QStringLiteral("ratio"), compressor, &CompressorProcessor::setRatio);
    chain.bind(QStringLiteral("attack"), compressor, &CompressorProcessor::setAttackMs);
    chain.bind(QStringLiteral("release"), compressor, &CompressorProcessor::setReleaseMs);
    chain.bind(QStringLiteral("makeup"), compressor, &CompressorProcessor::setMakeupLinear);
}

// texture.tape: the same compressor, but its manifest stores a linear threshold.
void buildTape(ChainProcessor &chain)
{
    auto *compressor = chain.addStage<CompressorProcessor>();
    chain.bind(QStringLiteral("threshold"),
               [compressor](float v) { compressor->setThresholdDb(linearToDb(v)); });
    chain.bind(QStringLiteral("ratio"), compressor, &CompressorProcessor::setRatio);
    chain.bind(QStringLiteral("attack"), compressor, &CompressorProcessor::setAttackMs);
    chain.bind(QStringLiteral("release"), compressor, &CompressorProcessor::setReleaseMs);
}

void buildLimiter(ChainProcessor &chain)
{
    auto *limiter = chain.addStage<LimiterProcessor>();
    chain.bind(QStringLiteral("drive"), limiter, &LimiterProcessor::setDriveLinear);
    chain.bind(QStringLiteral("ceiling"), limiter, &LimiterProcessor::setCeilingLinear);
}

void buildGate(ChainProcessor &chain)
{
    auto *gate = chain.addStage<GateProcessor>();
    chain.bind(QStringLiteral("threshold"), gate, &GateProcessor::setThresholdLinear);
    chain.bind(QStringLiteral("ratio"), gate, &GateProcessor::setRatio);
    chain.bind(QStringLiteral("attack"), gate, &GateProcessor::setAttackMs);
    chain.bind(QStringLiteral("release"), gate, &GateProcessor::setReleaseMs);
}

void buildDeEsser(ChainProcessor &chain)
{
    auto *deesser = chain.addStage<DeEsserProcessor>();
    chain.bind(QStringLiteral("intensity"), deesser, &DeEsserProcessor::setIntensity);
    chain.bind(QStringLiteral("amount"), deesser, &DeEsserProcessor::setAmount);
    chain.bind(QStringLiteral("frequency"), deesser, &DeEsserProcessor::setFrequency);
}

void buildLeveler(ChainProcessor &chain)
{
    auto *leveler = chain.addStage<LevelerProcessor>();
    chain.bind(QStringLiteral("strength"), leveler, &LevelerProcessor::setStrength);
    chain.bind(QStringLiteral("peak"), leveler, &LevelerProcessor::setPeak);
}

// ---- voice ------------------------------------------------------------------------------

void buildPitch(ChainProcessor &chain)
{
    auto *pitch = chain.addStage<SoundTouchPitchProcessor>();
    chain.bind(QStringLiteral("pitch"), pitch, &SoundTouchPitchProcessor::setRatio);
}

// voice.vader: asetrate/aresample/atempo + aecho=0.8:0.9 + acrusher=bits=10.
void buildDarkLord(ChainProcessor &chain)
{
    auto *pitch = chain.addStage<SoundTouchPitchProcessor>();
    auto *echo = chain.addStage<EchoProcessor>();
    auto *crusher = chain.addStage<BitCrusherProcessor>(BitCrusherProcessor::Mode::Linear, 10.0f);

    echo->setInGain(0.8f);
    echo->setOutGain(0.9f);

    chain.bind(QStringLiteral("pitch"), pitch, &SoundTouchPitchProcessor::setRatio);
    chain.bind(QStringLiteral("echo_delay"), echo, &EchoProcessor::setDelayMs);
    chain.bind(QStringLiteral("echo_decay"), echo, &EchoProcessor::setDecay);
    chain.bind(QStringLiteral("grit"), crusher, &BitCrusherProcessor::setMix);
}

// space.tremolo uses "rate", voice.robot uses "freq"; both drive the same LFO.
void buildTremolo(ChainProcessor &chain)
{
    auto *tremolo = chain.addStage<TremoloProcessor>();
    chain.bind(QStringLiteral("rate"), tremolo, &TremoloProcessor::setRate);
    chain.bind(QStringLiteral("freq"), tremolo, &TremoloProcessor::setRate);
    chain.bind(QStringLiteral("depth"), tremolo, &TremoloProcessor::setDepth);
}

void buildVibrato(ChainProcessor &chain)
{
    auto *vibrato = chain.addStage<VibratoProcessor>();
    chain.bind(QStringLiteral("rate"), vibrato, &VibratoProcessor::setRate);
    chain.bind(QStringLiteral("depth"), vibrato, &VibratoProcessor::setDepth);
}

// voice.alien binds speed/delay/depth/regen; space.flanger adds rate/mix/phase/invert.
void buildFlanger(ChainProcessor &chain)
{
    auto *flanger = chain.addStage<FlangerProcessor>();
    chain.bind(QStringLiteral("speed"), flanger, &FlangerProcessor::setRate);
    chain.bind(QStringLiteral("rate"), flanger, &FlangerProcessor::setRate);
    chain.bind(QStringLiteral("delay"), flanger, &FlangerProcessor::setDelayMs);
    chain.bind(QStringLiteral("depth"), flanger, &FlangerProcessor::setDepthMs);
    chain.bind(QStringLiteral("regen"), flanger, &FlangerProcessor::setRegenPercent);
    chain.bind(QStringLiteral("mix"), flanger, &FlangerProcessor::setWidthPercent);
    chain.bind(QStringLiteral("phase"), flanger, &FlangerProcessor::setPhaseDegrees);
    chain.bind(QStringLiteral("invert"), flanger, &FlangerProcessor::setInvertRight);
}

// ---- transmission -----------------------------------------------------------------------

void buildBandLimit(ChainProcessor &chain)
{
    auto *highPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::HighPass, 300.0f);
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 3400.0f);
    chain.bind(QStringLiteral("low_cut"), highPass, &BandFilterProcessor::setFrequency);
    chain.bind(QStringLiteral("high_cut"), lowPass, &BandFilterProcessor::setFrequency);
}

void buildWalkie(ChainProcessor &chain)
{
    auto *highPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::HighPass, 400.0f);
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 3000.0f);
    auto *crusher = chain.addStage<BitCrusherProcessor>(BitCrusherProcessor::Mode::Linear, 6.0f);
    chain.bind(QStringLiteral("low_cut"), highPass, &BandFilterProcessor::setFrequency);
    chain.bind(QStringLiteral("high_cut"), lowPass, &BandFilterProcessor::setFrequency);
    chain.bind(QStringLiteral("grit"), crusher, &BitCrusherProcessor::setMix);
}

void buildMegaphone(ChainProcessor &chain)
{
    auto *band = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::BandPass, 1500.0f);
    auto *crusher = chain.addStage<BitCrusherProcessor>(BitCrusherProcessor::Mode::Linear, 8.0f);
    chain.bind(QStringLiteral("center"), band, &BandFilterProcessor::setFrequency);
    chain.bind(QStringLiteral("width"), band, &BandFilterProcessor::setWidthHz);
    chain.bind(QStringLiteral("grit"), crusher, &BitCrusherProcessor::setMix);
}

// transmission.underwater: lowpass + chorus={wet}:0.9:50:0.4:{motion}:2.
void buildUnderwater(ChainProcessor &chain)
{
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 500.0f);
    auto *chorus = chain.addStage<ChorusProcessor>();

    chorus->setOutGain(0.9f);
    chorus->setDelayMs(50.0f);
    chorus->setDecay(0.4f);
    chorus->setDepthMs(2.0f);

    chain.bind(QStringLiteral("cutoff"), lowPass, &BandFilterProcessor::setFrequency);
    chain.bind(QStringLiteral("wet"), chorus, &ChorusProcessor::setInGain);
    chain.bind(QStringLiteral("motion"), chorus, &ChorusProcessor::setRate);
}

void buildMuffled(ChainProcessor &chain)
{
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 800.0f);
    auto *gain = chain.addStage<GainProcessor>();
    chain.bind(QStringLiteral("cutoff"), lowPass, &BandFilterProcessor::setFrequency);
    chain.bind(QStringLiteral("gain"), gain, &GainProcessor::setGain);
}

// ---- texture ----------------------------------------------------------------------------

Builder makeBitCrushBuilder(BitCrusherProcessor::Mode mode)
{
    return [mode](ChainProcessor &chain) {
        auto *crusher = chain.addStage<BitCrusherProcessor>(mode);
        chain.bind(QStringLiteral("bits"), crusher, &BitCrusherProcessor::setBits);
        chain.bind(QStringLiteral("samples"), crusher, &BitCrusherProcessor::setSampleReduction);
        chain.bind(QStringLiteral("mix"), crusher, &BitCrusherProcessor::setMix);
    };
}

void buildVinyl(ChainProcessor &chain)
{
    auto *highPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::HighPass, 200.0f);
    auto *lowPass = chain.addStage<BandFilterProcessor>(BandFilterProcessor::Mode::LowPass, 6000.0f);
    auto *vibrato = chain.addStage<VibratoProcessor>();
    chain.bind(QStringLiteral("highpass"), highPass, &BandFilterProcessor::setFrequency);
    chain.bind(QStringLiteral("lowpass"), lowPass, &BandFilterProcessor::setFrequency);
    chain.bind(QStringLiteral("wobble"), vibrato, &VibratoProcessor::setRate);
    chain.bind(QStringLiteral("flutter"), vibrato, &VibratoProcessor::setDepth);
}

void buildCrystalizer(ChainProcessor &chain)
{
    auto *crystal = chain.addStage<CrystalizerProcessor>();
    chain.bind(QStringLiteral("intensity"), crystal, &CrystalizerProcessor::setIntensity);
    chain.bind(QStringLiteral("colors"), crystal, &CrystalizerProcessor::setColors);
}

// ---- space ------------------------------------------------------------------------------

void buildEcho(ChainProcessor &chain)
{
    auto *echo = chain.addStage<EchoProcessor>();
    chain.bind(QStringLiteral("delay"), echo, &EchoProcessor::setDelayMs);
    chain.bind(QStringLiteral("decay"), echo, &EchoProcessor::setDecay);
    chain.bind(QStringLiteral("in_gain"), echo, &EchoProcessor::setInGain);
    chain.bind(QStringLiteral("out_gain"), echo, &EchoProcessor::setOutGain);
}

void buildChorus(ChainProcessor &chain)
{
    auto *chorus = chain.addStage<ChorusProcessor>();
    chain.bind(QStringLiteral("in_gain"), chorus, &ChorusProcessor::setInGain);
    chain.bind(QStringLiteral("out_gain"), chorus, &ChorusProcessor::setOutGain);
    chain.bind(QStringLiteral("delay"), chorus, &ChorusProcessor::setDelayMs);
    chain.bind(QStringLiteral("decay"), chorus, &ChorusProcessor::setDecay);
    chain.bind(QStringLiteral("speed"), chorus, &ChorusProcessor::setRate);
    chain.bind(QStringLiteral("depth"), chorus, &ChorusProcessor::setDepthMs);
}

void buildPhaser(ChainProcessor &chain)
{
    auto *phaser = chain.addStage<PhaserProcessor>();
    chain.bind(QStringLiteral("in_gain"), phaser, &PhaserProcessor::setInGain);
    chain.bind(QStringLiteral("out_gain"), phaser, &PhaserProcessor::setOutGain);
    chain.bind(QStringLiteral("delay"), phaser, &PhaserProcessor::setDelayMs);
    chain.bind(QStringLiteral("decay"), phaser, &PhaserProcessor::setDecay);
    chain.bind(QStringLiteral("speed"), phaser, &PhaserProcessor::setRate);
}

void buildAutoPan(ChainProcessor &chain)
{
    auto *pan = chain.addStage<AutoPanProcessor>();
    chain.bind(QStringLiteral("rate"), pan, &AutoPanProcessor::setRate);
    chain.bind(QStringLiteral("amount"), pan, &AutoPanProcessor::setAmount);
    chain.bind(QStringLiteral("level_in"), pan, &AutoPanProcessor::setLevelIn);
    chain.bind(QStringLiteral("level_out"), pan, &AutoPanProcessor::setLevelOut);
}

void buildStereoWiden(ChainProcessor &chain)
{
    auto *widen = chain.addStage<StereoWidenProcessor>();
    chain.bind(QStringLiteral("delay"), widen, &StereoWidenProcessor::setDelayMs);
    chain.bind(QStringLiteral("feedback"), widen, &StereoWidenProcessor::setFeedback);
    chain.bind(QStringLiteral("crossfeed"), widen, &StereoWidenProcessor::setCrossfeed);
    chain.bind(QStringLiteral("drymix"), widen, &StereoWidenProcessor::setDryMix);
}

const QHash<QString, Builder> &registry()
{
    static const QHash<QString, Builder> builders{
        {QStringLiteral("eq3"), buildEq3},
        {QStringLiteral("compressor"), buildCompressor},
        {QStringLiteral("tape"), buildTape},
        {QStringLiteral("limiter"), buildLimiter},
        {QStringLiteral("gate"), buildGate},
        {QStringLiteral("deesser"), buildDeEsser},
        {QStringLiteral("leveler"), buildLeveler},
        {QStringLiteral("pitch"), buildPitch},
        {QStringLiteral("darklord"), buildDarkLord},
        {QStringLiteral("tremolo"), buildTremolo},
        {QStringLiteral("vibrato"), buildVibrato},
        {QStringLiteral("flanger"), buildFlanger},
        {QStringLiteral("bandlimit"), buildBandLimit},
        {QStringLiteral("walkie"), buildWalkie},
        {QStringLiteral("megaphone"), buildMegaphone},
        {QStringLiteral("underwater"), buildUnderwater},
        {QStringLiteral("muffled"), buildMuffled},
        {QStringLiteral("bitcrush"), makeBitCrushBuilder(BitCrusherProcessor::Mode::Linear)},
        {QStringLiteral("bitcrush_log"), makeBitCrushBuilder(BitCrusherProcessor::Mode::Logarithmic)},
        {QStringLiteral("vinyl"), buildVinyl},
        {QStringLiteral("crystalizer"), buildCrystalizer},
        {QStringLiteral("echo"), buildEcho},
        {QStringLiteral("chorus"), buildChorus},
        {QStringLiteral("phaser"), buildPhaser},
        {QStringLiteral("autopan"), buildAutoPan},
        {QStringLiteral("stereowiden"), buildStereoWiden},
    };
    return builders;
}

} // namespace

std::unique_ptr<ChainProcessor> createProcessor(const QString &processorId)
{
    const auto it = registry().constFind(processorId);
    if (it == registry().constEnd())
        return nullptr;

    auto chain = std::make_unique<ChainProcessor>();
    it.value()(*chain);
    return chain;
}

bool hasProcessor(const QString &processorId)
{
    return registry().contains(processorId);
}

QStringList processorIds()
{
    QStringList ids = registry().keys();
    ids.sort();
    return ids;
}

} // namespace drift::audiofx
