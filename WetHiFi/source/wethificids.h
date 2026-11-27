//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "pluginterfaces/base/funknown.h"
#include "pluginterfaces/vst/vsttypes.h"

namespace Yonie {

//------------------------------------------------------------------------
static const Steinberg::FUID kWetHiFiProcessorUID (0x4B9E2D71, 0xA6034C58, 0x9E17F3B2, 0x5C80D64A);
static const Steinberg::FUID kWetHiFiControllerUID (0xD3527A0E, 0x61BF4E93, 0x8A2C05F7, 0x3E94B1C6);

#define WetHiFiVST3Category "Fx|EQ"

//------------------------------------------------------------------------
// Parameter IDs
//
// Three boost knobs, top to bottom on the panel: SILKY HIGHS, FOCUSED MIDS,
// TIGHT BASS. Each is a step index 0..40 (41 fine positions, 11 printed).
//------------------------------------------------------------------------
enum WetHiFiParams : Steinberg::Vst::ParamID
{
    kHighsParam = 0,
    kMidsParam  = 1,
    kBassParam  = 2,

    // Output-only, for the lamp: the output's loudness with VU-like
    // ballistics, 0..1. The lamp's COLOUR comes from the knobs alone.
    kLampLevel  = 3,

    // Output-only: the output's peak for the single LED top right, encoded as
    // the line's meters encode it (meterlevel.h).
    kOutPeak    = 4,

    kParamCount = 5
};

//------------------------------------------------------------------------
} // namespace Yonie
