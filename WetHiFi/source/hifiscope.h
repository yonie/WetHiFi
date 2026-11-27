//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------
// The screen's data, as it travels from the processor to the editor through
// the SDK's data exchange (or its message fallback on older hosts).
//------------------------------------------------------------------------

#pragma once

#include <cstdint>

namespace Yonie {

// About 60 snapshots a second: the newest stretch of the output summed to
// mono, at the full sample rate. The editor finds its own trigger for the
// oscilloscope and runs its own FFT for the spectrogram and the analyser.
struct ScopeBlock
{
    static constexpr int kSamples = 2048;
    uint32_t count = 0;                     // valid samples
    uint32_t fresh = 0;                     // how many at the end are new since the last block
    float sampleRate = 48000.0f;
    float samples[kSamples] = {};           // oldest first
};

static constexpr uint32_t kScopeQueueID = 1;

} // namespace Yonie
