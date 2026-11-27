//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "wethifiprocessor.h"
#include "wethificids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <algorithm>
#include <cmath>

using namespace Steinberg;

namespace Yonie {

//------------------------------------------------------------------------
WetHiFiProcessor::WetHiFiProcessor()
{
    setControllerClass(kWetHiFiControllerUID);
}

//------------------------------------------------------------------------
WetHiFiProcessor::~WetHiFiProcessor() {}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::initialize(FUnknown* context)
{
    tresult result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addAudioInput(STR16("Stereo In"), Steinberg::Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);
    return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::terminate()
{
    return AudioEffect::terminate();
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::setActive(TBool state)
{
    if (state)
    {
        engine.reset();
        vuPower = 0.0;
        oldLamp = -1.0f;
        outPeak = 0.0f;
        oldPeak = -1.0f;
        std::fill(scopeRing.begin(), scopeRing.end(), 0.0f);
        scopeSince = 0;
        scopeExchange.onActivate(processSetup);
    }
    else
    {
        scopeExchange.onDeactivate();
    }
    return AudioEffect::setActive(state);
}

//------------------------------------------------------------------------
// All four mono/stereo layouts are accepted; monobus.h turns mono into the
// stereo the processor expects.
tresult PLUGIN_API WetHiFiProcessor::setBusArrangements(Vst::SpeakerArrangement* inputs, int32 numIns,
                                            Vst::SpeakerArrangement* outputs, int32 numOuts)
{
    if (!Wet::MonoBus::accepts(inputs, numIns, outputs, numOuts))
        return kResultFalse;
    return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::setupProcessing(Vst::ProcessSetup& newSetup)
{
    engine.prepare(newSetup.sampleRate, newSetup.maxSamplesPerBlock);
    scopeRing.assign(16384, 0.0f);
    scopePos = 0;
    scopeHop = std::max(1, static_cast<int>(newSetup.sampleRate / 60.0));
    vuAttack = 1.0 - std::exp(-1.0 / (newSetup.sampleRate * 0.010));    // 10 ms rise
    vuRelease = 1.0 - std::exp(-1.0 / (newSetup.sampleRate * 0.300));   // 300 ms fall
    monoBus.prepare(newSetup.maxSamplesPerBlock);
    return AudioEffect::setupProcessing(newSetup);
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

//------------------------------------------------------------------------
static int toStep(Vst::ParamValue normalized)
{
    int idx = static_cast<int>(normalized * (HiFiRange::kSteps - 1) + 0.5);
    if (idx < 0) idx = 0;
    if (idx > HiFiRange::kSteps - 1) idx = HiFiRange::kSteps - 1;
    return idx;
}

//------------------------------------------------------------------------
void WetHiFiProcessor::applyParameter(Vst::ParamID id, Vst::ParamValue value)
{
    switch (id)
    {
        case kHighsParam: pending.highs = toStep(value); break;
        case kMidsParam:  pending.mids  = toStep(value); break;
        case kBassParam:  pending.bass  = toStep(value); break;
        default: break;
    }
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::process(Vst::ProcessData& data)
{
    if (data.inputParameterChanges)
    {
        const int32 numChanged = data.inputParameterChanges->getParameterCount();
        for (int32 index = 0; index < numChanged; ++index)
        {
            if (auto* queue = data.inputParameterChanges->getParameterData(index))
            {
                const int32 numPoints = queue->getPointCount();
                if (numPoints <= 0)
                    continue;
                Vst::ParamValue value;
                int32 sampleOffset;
                // Last point in the block: the engine glides every gain itself.
                if (queue->getPoint(numPoints - 1, sampleOffset, value) == kResultTrue)
                    applyParameter(queue->getParameterId(), value);
            }
        }
        engine.setSettings(pending);
    }

    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples <= 0)
        return kResultOk;

    Vst::AudioBusBuffers& input = data.inputs[0];
    Vst::AudioBusBuffers& output = data.outputs[0];

    float *inL, *inR, *outL, *outR;
    if (!monoBus.begin(input, output, data.numSamples, inL, inR, outL, outR))
        return kResultOk;

    engine.processStereo(inL, inR, outL, outR, data.numSamples);
    feedScope(outL, outR, data.numSamples);

    // The lamp's brightness, like a VU meter on the output (Ronald, 2026-11-20:
    // "it should light up a bit more on every kick"). Power of the output, a
    // fast rise so a kick is caught and a VU-like fall of ~300 ms, mapped from
    // -40 dBFS (dark) to -8 dBFS (full).
    for (int32 i = 0; i < data.numSamples; ++i)
    {
        const double p = 0.5 * (double(outL[i]) * outL[i] + double(outR[i]) * outR[i]);
        vuPower += (p - vuPower) * (p > vuPower ? vuAttack : vuRelease);
    }
    if (vuPower < 1e-12)
        vuPower = 0.0;
    // The LED: the output's peak, held and falling the way every meter in the
    // line does (WetChorus's updatePeak).
    for (int32 i = 0; i < data.numSamples; ++i)
    {
        const float a = std::max(std::fabs(outL[i]), std::fabs(outR[i]));
        outPeak = a > outPeak ? a : outPeak * kMeterDecay;
    }
    if (outPeak < 1e-9f)
        outPeak = 0.0f;
    if (data.outputParameterChanges)
    {
        const float v = Meter::encode(outPeak);
        if (v != oldPeak)
        {
            int32 index = 0;
            if (auto* queue = data.outputParameterChanges->addParameterData(kOutPeak, index))
            {
                int32 pointIndex = 0;
                queue->addPoint(0, v, pointIndex);
            }
            oldPeak = v;
        }
    }
    if (data.outputParameterChanges)
    {
        const double db = vuPower > 0.0 ? 10.0 * std::log10(vuPower) : -120.0;
        float v = float(std::clamp((db + 40.0) / 32.0, 0.0, 1.0));
        v = std::round(v * 500.0f) / 500.0f;
        if (v != oldLamp)
        {
            int32 index = 0;
            if (auto* queue = data.outputParameterChanges->addParameterData(kLampLevel, index))
            {
                int32 pointIndex = 0;
                queue->addPoint(0, v, pointIndex);
            }
            oldLamp = v;
        }
    }

    monoBus.end(output, data.numSamples, outL, outR);
    output.silenceFlags = 0;
    return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::connect(Vst::IConnectionPoint* other)
{
    auto res = AudioEffect::connect(other);
    scopeExchange.onConnect(other, getHostContext());
    return res;
}

tresult PLUGIN_API WetHiFiProcessor::disconnect(Vst::IConnectionPoint* other)
{
    scopeExchange.onDisconnect(other);
    return AudioEffect::disconnect(other);
}

//------------------------------------------------------------------------
// The screen: keep the mono output in a ring and, about 60 times a
// second, send the editor the newest stretch of it at the full rate.
//
void WetHiFiProcessor::feedScope(const float* l, const float* r, int n)
{
    if (scopeRing.empty())
        return;
    const size_t N = scopeRing.size();
    for (int i = 0; i < n; ++i)
    {
        scopeRing[scopePos] = 0.5f * (l[i] + r[i]);
        scopePos = (scopePos + 1) % N;
    }
    scopeSince += n;
    if (scopeSince < scopeHop)
        return;
    const int fresh = scopeSince;
    scopeSince = 0;

    auto block = scopeExchange.getCurrentOrNewBlock();
    if (block.blockID == Vst::InvalidDataExchangeBlockID || !block.data)
        return;
    auto* out = static_cast<ScopeBlock*>(block.data);

    // The newest kSamples at the full rate, oldest first. The editor triggers
    // the oscilloscope itself and runs its own FFT for the other two screens.
    const size_t span = size_t(ScopeBlock::kSamples);
    const size_t start = (scopePos + N - span) % N;
    for (size_t p = 0; p < span; ++p)
        out->samples[p] = scopeRing[(start + p) % N];
    out->count = ScopeBlock::kSamples;
    out->fresh = static_cast<uint32_t>(std::min(fresh, ScopeBlock::kSamples));
    out->sampleRate = static_cast<float>(processSetup.sampleRate);
    scopeExchange.sendCurrentBlock();
}

//------------------------------------------------------------------------
// State, version 1: version, the step count the indices are written on, then
// the three indices top to bottom (highs, mids, bass). The step count is there
// so a later build with a different grid can rescale instead of misreading.
//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if (!streamer.readInt32(version))
        return kResultFalse;

    int32 savedSteps = 0;
    if (!streamer.readInt32(savedSteps) || savedSteps <= 1)
        savedSteps = HiFiRange::kSteps;

    HiFiEngine::Settings s;
    int32 v = 0;
    auto rd = [&](int& dst) {
        if (streamer.readInt32(v))
            dst = HiFiRange::rescaleStep(v, savedSteps);
    };
    rd(s.highs);
    rd(s.mids);
    rd(s.bass);

    pending = s;
    engine.setSettings(s);
    return kResultOk;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiProcessor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);
    streamer.writeInt32(1);
    streamer.writeInt32(HiFiRange::kSteps);

    const HiFiEngine::Settings& s = engine.settings();
    streamer.writeInt32(s.highs);
    streamer.writeInt32(s.mids);
    streamer.writeInt32(s.bass);
    return kResultOk;
}

//------------------------------------------------------------------------
} // namespace Yonie
