//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "wethificontroller.h"
#include "hifieditor.h"
#include "wethificids.h"
#include "hifiengine.h"
#include "customviewcreator.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/base/ustring.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace Steinberg;

namespace Yonie {

namespace {

//------------------------------------------------------------------------
// A stepped 0-10 knob, printed as the panel prints it: 41 fine positions,
// so a quarter of a number apart.
//------------------------------------------------------------------------
class KnobParameter : public Vst::Parameter
{
public:
    KnobParameter(const Vst::TChar* title, Vst::ParamID id)
    {
        Vst::ParameterInfo& i = info;
        UString(i.title, str16BufferSize(Vst::String128)).assign(title);
        i.id = id;
        i.stepCount = HiFiRange::kSteps - 1;
        i.defaultNormalizedValue = 0.0;
        i.unitId = Vst::kRootUnitId;
        i.flags = Vst::ParameterInfo::kCanAutomate;
        setNormalized(i.defaultNormalizedValue);
    }

    static int stepOf(Vst::ParamValue normalized)
    {
        int idx = static_cast<int>(normalized * (HiFiRange::kSteps - 1) + 0.5);
        if (idx < 0) idx = 0;
        if (idx > HiFiRange::kSteps - 1) idx = HiFiRange::kSteps - 1;
        return idx;
    }

    static double scaleOf(int step)
    {
        return 10.0 * step / (HiFiRange::kSteps - 1);
    }

    void toString(Vst::ParamValue normalized, Vst::String128 string) const SMTG_OVERRIDE
    {
        char text[32];
        std::snprintf(text, sizeof(text), "%.2f", scaleOf(stepOf(normalized)));
        UString(string, str16BufferSize(Vst::String128)).fromAscii(text);
    }

    bool fromString(const Vst::TChar* string, Vst::ParamValue& normalized) const SMTG_OVERRIDE
    {
        if (!string)
            return false;
        UString wrapper(const_cast<Vst::TChar*>(string), str16BufferSize(Vst::String128));
        double want = 0.0;
        if (!wrapper.scanFloat(want))
            return false;
        int step = static_cast<int>(std::lround(want / 10.0 * (HiFiRange::kSteps - 1)));
        if (step < 0) step = 0;
        if (step > HiFiRange::kSteps - 1) step = HiFiRange::kSteps - 1;
        normalized = static_cast<double>(step) / (HiFiRange::kSteps - 1);
        return true;
    }
};

} // namespace

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiController::initialize(FUnknown* context)
{
    tresult result = EditControllerEx1::initialize(context);
    if (result != kResultOk)
        return result;

    registerCustomViews();

    parameters.addParameter(new KnobParameter(reinterpret_cast<const Vst::TChar*>(u"Silky Highs"), kHighsParam));
    parameters.addParameter(new KnobParameter(reinterpret_cast<const Vst::TChar*>(u"Focused Mids"), kMidsParam));
    parameters.addParameter(new KnobParameter(reinterpret_cast<const Vst::TChar*>(u"Tight Bass"), kBassParam));

    // The lamp's feed. Read-only so a host never tries to automate it.
    parameters.addParameter(STR16("Lamp Level"), nullptr, 0, 0, Vst::ParameterInfo::kIsReadOnly, kLampLevel);
    parameters.addParameter(STR16("Output Peak"), nullptr, 0, 0, Vst::ParameterInfo::kIsReadOnly, kOutPeak);

    return result;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiController::terminate()
{
    return EditControllerEx1::terminate();
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiController::setComponentState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    IBStreamer streamer(state, kLittleEndian);

    int32 version = 0;
    if (!streamer.readInt32(version))
        return kResultFalse;

    // Same read as WetHiFiProcessor::setState, and it has to stay the same.
    int32 savedSteps = 0;
    if (!streamer.readInt32(savedSteps) || savedSteps <= 1)
        savedSteps = HiFiRange::kSteps;

    int32 v = 0;
    auto restore = [&](Vst::ParamID id) {
        if (streamer.readInt32(v))
        {
            const int idx = HiFiRange::rescaleStep(v, savedSteps);
            setParamNormalized(id, static_cast<double>(idx) / (HiFiRange::kSteps - 1));
        }
    };
    restore(kHighsParam);
    restore(kMidsParam);
    restore(kBassParam);
    return kResultOk;
}

//------------------------------------------------------------------------
IPlugView* PLUGIN_API WetHiFiController::createView(FIDString name)
{
    if (FIDStringsEqual(name, Vst::ViewType::kEditor))
    {
        auto* editor = new Yonie::HiFiEditor(this, "view", "wethifieditor.uidesc");
        editor->setAllowedZoomFactors({0.5, 0.75, 1.0, 1.25});
        editor->setZoomFactor(0.75);
        return editor;
    }
    return nullptr;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiController::notify(Vst::IMessage* message)
{
    // hosts without the data-exchange interface carry the scope as messages
    if (scopeReceiver.onMessage(message))
        return kResultTrue;
    return EditControllerEx1::notify(message);
}

void PLUGIN_API WetHiFiController::queueOpened(Vst::DataExchangeUserContextID, uint32,
                                               TBool& dispatchOnBackgroundThread)
{
    // on the UI thread, where the scope reads it: no locking needed
    dispatchOnBackgroundThread = false;
}

void PLUGIN_API WetHiFiController::queueClosed(Vst::DataExchangeUserContextID) {}

void PLUGIN_API WetHiFiController::onDataExchangeBlocksReceived(Vst::DataExchangeUserContextID userContextID,
                                                                uint32 numBlocks,
                                                                Vst::DataExchangeBlock* blocks, TBool)
{
    if (userContextID != kScopeQueueID || numBlocks == 0)
        return;
    // only the newest snapshot matters
    const auto& b = blocks[numBlocks - 1];
    if (!b.data || b.size < sizeof(ScopeBlock))
        return;
    const auto* in = static_cast<const ScopeBlock*>(b.data);
    scopeN = std::min<uint32_t>(in->count, ScopeBlock::kSamples);
    std::copy(in->samples, in->samples + scopeN, scopeData.begin());
    if (in->sampleRate > 1000.0f)
        scopeSampleRate = in->sampleRate;
    // The longer history the analyser needs for its low end: append what is new
    // in this block to a ring of kHistory samples.
    const uint32_t fresh = std::min(in->fresh, scopeN);
    for (uint32_t i = scopeN - fresh; i < scopeN; ++i)
    {
        history[histPos] = in->samples[i];
        histPos = (histPos + 1) % kHistory;
    }
    ++serial;
}

//------------------------------------------------------------------------
// The controller's own state: only which screen the round window shows. It is
// separate from the processor's state and touches nothing that sounds.
// Version 1: int32 version, int32 screen (0 scope, 1 spectrogram, 2 analyser).
tresult PLUGIN_API WetHiFiController::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;
    IBStreamer streamer(state, kLittleEndian);
    int32 version = 0, m = 0;
    if (streamer.readInt32(version) && streamer.readInt32(m))
        setViewMode(m);
    return kResultTrue;
}

//------------------------------------------------------------------------
tresult PLUGIN_API WetHiFiController::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;
    IBStreamer streamer(state, kLittleEndian);
    streamer.writeInt32(1);
    streamer.writeInt32(mode);
    return kResultTrue;
}

//------------------------------------------------------------------------
} // namespace Yonie
