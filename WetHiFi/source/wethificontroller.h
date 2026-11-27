//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "public.sdk/source/vst/utility/dataexchange.h"
#include "pluginterfaces/vst/ivstdataexchange.h"
#include "hifiscope.h"

#include <array>
#include <cstdint>

namespace Yonie {

//------------------------------------------------------------------------
class WetHiFiController : public Steinberg::Vst::EditControllerEx1,
                          public Steinberg::Vst::IDataExchangeReceiver
{
public:
    WetHiFiController() = default;
    ~WetHiFiController() SMTG_OVERRIDE = default;

    static Steinberg::FUnknown* createInstance(void* /*context*/)
    {
        return (Steinberg::Vst::IEditController*)new WetHiFiController;
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API terminate() SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) SMTG_OVERRIDE;

    // IDataExchangeReceiver: the oscilloscope's snapshots from the processor
    void PLUGIN_API queueOpened(Steinberg::Vst::DataExchangeUserContextID userContextID,
                                Steinberg::uint32 blockSize,
                                Steinberg::TBool& dispatchOnBackgroundThread) SMTG_OVERRIDE;
    void PLUGIN_API queueClosed(Steinberg::Vst::DataExchangeUserContextID userContextID) SMTG_OVERRIDE;
    void PLUGIN_API onDataExchangeBlocksReceived(Steinberg::Vst::DataExchangeUserContextID userContextID,
                                                 Steinberg::uint32 numBlocks,
                                                 Steinberg::Vst::DataExchangeBlock* blocks,
                                                 Steinberg::TBool onBackgroundThread) SMTG_OVERRIDE;

    // The latest snapshot, read by the scope on the UI thread. `serial` goes up
    // by one with every new snapshot, so the view redraws only when it changed.
    const std::array<float, ScopeBlock::kSamples>& scope() const { return scopeData; }
    float scopeRate() const { return scopeSampleRate; }

    // Which screen the round window shows: 0 oscilloscope, 1 spectrogram,
    // 2 spectrum analyser. Saved with the project (controller state).
    int viewMode() const { return mode; }
    void setViewMode(int m) { mode = (m % 3 + 3) % 3; }
    uint32_t scopeCount() const { return scopeN; }

    // The newest n samples of the longer history, oldest first (n <= kHistory).
    static constexpr int kHistory = 8192;
    void copyHistory(float* dst, int n) const
    {
        for (int i = 0; i < n; ++i)
            dst[i] = history[(histPos + kHistory - n + i) % kHistory];
    }
    uint64_t scopeSerial() const { return serial; }

    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IDataExchangeReceiver)
    END_DEFINE_INTERFACES(Steinberg::Vst::EditControllerEx1)
    DELEGATE_REFCOUNT(Steinberg::Vst::EditControllerEx1)

private:
    Steinberg::Vst::DataExchangeReceiverHandler scopeReceiver {this};
    std::array<float, ScopeBlock::kSamples> scopeData {};
    float scopeSampleRate = 48000.0f;
    int mode = 0;
    std::array<float, kHistory> history {};
    int histPos = 0;
    uint32_t scopeN = 0;
    uint64_t serial = 0;
};

//------------------------------------------------------------------------
} // namespace Yonie
