//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "hifiengine.h"
#include "hifiscope.h"
#include "wethificids.h"
#include "monobus.h"
#include "meterlevel.h"

#include <vector>

namespace Yonie {

//------------------------------------------------------------------------
class WetHiFiProcessor : public Steinberg::Vst::AudioEffect
{
public:
    WetHiFiProcessor();
    ~WetHiFiProcessor() SMTG_OVERRIDE;

    static Steinberg::FUnknown* createInstance(void* /*context*/)
    {
        return (Steinberg::Vst::IAudioProcessor*)new WetHiFiProcessor;
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& newSetup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setBusArrangements(Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
                                                 Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) SMTG_OVERRIDE;
    Wet::MonoBus monoBus;   // mono inputs and outputs, see monobus.h
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) SMTG_OVERRIDE;

protected:
    // The oscilloscope feed: the mono output in a ring, and a snapshot of it
    // handed to the editor about 60 times a second.
    void feedScope(const float* l, const float* r, int n);
    Steinberg::Vst::DataExchangeHandler scopeExchange {
        this, [](Steinberg::Vst::DataExchangeHandler::Config& c, const Steinberg::Vst::ProcessSetup&) {
            c.blockSize = sizeof(ScopeBlock);
            c.numBlocks = 6;
            c.alignment = 32;
            c.userContextID = kScopeQueueID;
            return true;
        }};
    std::vector<float> scopeRing;
    size_t scopePos = 0;
    int scopeHop = 800;
    int scopeSince = 0;

    void applyParameter(Steinberg::Vst::ParamID id, Steinberg::Vst::ParamValue value);

    HiFiEngine engine;
    HiFiEngine::Settings pending;
    // the lamp's VU: output power, smoothed with a fast rise and a slow fall
    double vuPower = 0.0, vuAttack = 0.1, vuRelease = 0.001;
    float oldLamp = -1.0f;
    // the output peak for the LED: instant rise, the line meters' decay
    float outPeak = 0.0f;
    float oldPeak = -1.0f;
    static constexpr float kMeterDecay = 0.9995f;
};

//------------------------------------------------------------------------
} // namespace Yonie
