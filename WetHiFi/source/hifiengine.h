//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//
// WetHiFi's DSP. No VST3 headers, so tools/hifitest can compile and measure
// this directly without loading a plugin host.
//
// A hi-fi "loudness" equaliser with three boost knobs. Each one BOOSTS its
// band and CUTS a neighbouring region on the same turn, so turning up shapes
// the sound rather than only adding level:
//
//   TIGHT BASS     low shelf around 45 Hz, up to +12 dB, with a broad bell
//                  CUT just above it near 180 Hz - the passive program-EQ
//                  trick of boosting and attenuating on one setting, which
//                  gives a deep bump with a dip above it.
//   FOCUSED MIDS   a broad bell around 1 kHz, up to +8 dB, with a small cut at
//                  3.2 kHz - the forward midrange of a small sealed nearfield.
//   SILKY HIGHS    a wide high shelf from about 10 kHz (+10 dB at 16 kHz), a
//                  small cut around 2.7 kHz against harshness, and a soft
//                  harmonic exciter on the band above 6 kHz, added in
//                  parallel so turning up adds sheen and not only level.
//
// Signal flow, per channel, everything in double:
//
//   in -> [bass shelf -> bass cut] -> [mid bell -> mid cut]
//      -> [air shelf -> presence cut] -> (+ exciter(in)) -> out
//
// and the exciter, which is the only nonlinear part:
//
//   in -> highpass 6 kHz -> up 2x -> harmonic generator -> down 2x
//      -> highpass 4.5 kHz -> x mix
//
// The harmonic generator outputs g(x) - x, the DISTORTION only, never the
// signal itself. That matters: the exciter path has the oversampling filters'
// delay in it, and adding a delayed copy of the band back onto the undelayed
// one would comb-filter it. Harmonics are new content, so a fraction of a
// millisecond of delay on them is nothing.
//
// Full rate, no resampling of the main path, no quantising, no lo-fi colour:
// this models a passive network and an amplifier, neither of which has a
// sample rate or a word length.
//
// With all three knobs at 0 the output is the input bit for bit: every EQ
// filter at exactly 0 dB passes its input through with a mix of 1, 0, 0, and
// the exciter is switched off. The filters keep running at 0 dB rather than
// being skipped, so a knob leaving 0 starts from live state, not a wake-up
// transient.
//
// Knob moves glide: a one-pole of ~20 ms on each band's dB, coefficients
// redesigned every 32 samples while it moves and ramped linearly sample by
// sample in between, so nothing zippers or clicks.
//
// Knob taper: boost dB = max * k^1.2, k = step / 40 (see boostFraction).
//------------------------------------------------------------------------

#pragma once

#include <atomic>
#include <cstdint>

namespace Yonie {

//------------------------------------------------------------------------
// Parameter grid. The panel prints 0..10 with eleven detents; the knob holds
// four fine positions per detent behind Shift, as every knob in the line does.
//------------------------------------------------------------------------
struct HiFiRange
{
    static constexpr int kSteps = 41;      // fine positions per knob
    static constexpr int kCoarse = 4;      // fine steps per printed detent

    // Map an index saved on a grid of savedSteps onto kSteps, rounding,
    // clamped. Saved state carries step indices, so a stream has to say which
    // grid it was written on and the reader rescales.
    static int rescaleStep(int v, int savedSteps);

    // Knob position 0..1 to the band's boost, as a fraction of its maximum.
    //
    // Straight, in dB: 5 on the panel is exactly half the boost (Ronald,
    // 2026-11-20 - no curve on the knob).
    static double boostFraction(double k);
};

inline int HiFiRange::rescaleStep(int v, int savedSteps)
{
    if (savedSteps <= 1) return 0;
    if (v < 0) v = 0;
    if (v > savedSteps - 1) v = savedSteps - 1;
    if (savedSteps == kSteps) return v;
    const int den = savedSteps - 1;
    int r = (v * (kSteps - 1) + den / 2) / den;
    if (r > kSteps - 1) r = kSteps - 1;
    return r;
}

//------------------------------------------------------------------------
class HiFiEngine
{
public:
    struct Settings { int highs = 0, mids = 0, bass = 0; };   // step indices 0..kSteps-1

    HiFiEngine();

    void prepare(double sampleRate, int maxBlock);
    void reset();
    void setSettings(const Settings& s);
    const Settings& settings() const { return target; }

    // In-place safe: out may alias in.
    void processStereo(const float* inL, const float* inR,
                       float* outL, float* outR, int n);

    // 0 highs, 1 mids, 2 bass; 0..1. Safe to call from any thread.
    float lampLevel(int band) const;

    // Maximum boost and cut per band, as printed at 10.
    static constexpr double kBassBoostDb   = 15.0;   // Ronald, 2026-11-20: a quarter more than the first build, linear
    static constexpr double kBassCutDb     = -5.6;
    static constexpr double kMidBoostDb    = 10.0;
    static constexpr double kMidCutDb      = -3.75;
    static constexpr double kAirBoostDb    = 16.25;  // shelf asymptote
    static constexpr double kPresenceCutDb = -2.5;

    enum Band { kHighs = 0, kMids = 1, kBass = 2, kBands = 3 };

private:
    //--------------------------------------------------------------------
    // The EQ filters: a trapezoidal state-variable filter. Same responses as
    // the bilinear cookbook biquads, but its states are the two integrators
    // of the analogue circuit, so changing the gain while audio runs through
    // it moves the output smoothly instead of kicking it - which a direct-form
    // biquad, whose states mean nothing on their own, does not.
    //
    // A new design is not switched in at once: the six coefficients ramp to
    // it linearly over the sub-block (rampLen samples), so a gain change in
    // the middle of a loud low note moves in steps of a few thousandths of a
    // decibel per sample rather than a few tenths per sub-block - which is
    // what zipper noise is. The ramp ends exactly on the design, so 0 dB is
    // exactly the identity again.
    struct Svf
    {
        enum { kA1, kA2, kA3, kM0, kM1, kM2, kN };
        double c[kN] = {1, 0, 0, 1, 0, 0};     // integrator coefficients, output mix
        double t[kN] = {1, 0, 0, 1, 0, 0};     // the design the ramp heads for
        double d[kN] = {};
        int ramp = 0;
        double ic1[2] = {0, 0}, ic2[2] = {0, 0};

        inline double run(double v0, int ch)
        {
            const double v3 = v0 - ic2[ch];
            const double v1 = c[kA1] * ic1[ch] + c[kA2] * v3;
            const double v2 = ic2[ch] + c[kA2] * ic1[ch] + c[kA3] * v3;
            ic1[ch] = 2.0 * v1 - ic1[ch];
            ic2[ch] = 2.0 * v2 - ic2[ch];
            return c[kM0] * v0 + c[kM1] * v1 + c[kM2] * v2;
        }
        // Once per sample frame, after both channels.
        inline void advance()
        {
            if (ramp <= 0) return;
            if (--ramp == 0)
                for (int i = 0; i < kN; ++i) c[i] = t[i];
            else
                for (int i = 0; i < kN; ++i) c[i] += d[i];
        }
        void clear() { ic1[0] = ic1[1] = ic2[0] = ic2[1] = 0.0; }
        void flush();

        void lowShelf(double fs, double f0, double q, double dB, int rampLen);
        void highShelf(double fs, double f0, double q, double dB, int rampLen);
        void bell(double fs, double f0, double q, double dB, int rampLen);
    private:
        void setG(double g, double k);
        void commit(int rampLen);
    };

    //--------------------------------------------------------------------
    // A fixed biquad, transposed direct form II, for the exciter's highpasses
    // (their coefficients never move). Coefficients shared by both channels,
    // a state per channel.
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double s1[2] = {0, 0}, s2[2] = {0, 0};

        inline double run(double x, int ch)
        {
            const double y = b0 * x + s1[ch];
            s1[ch] = b1 * x - a1 * y + s2[ch];
            s2[ch] = b2 * x - a2 * y;
            return y;
        }
        void clear() { s1[0] = s1[1] = s2[0] = s2[1] = 0.0; }
        void flush();

        void highShelf(double fs, double f0, double q, double dB);
        double magnitude(double fs, double f) const;
        void highpass(double fs, double f0, double q);
    };

    //--------------------------------------------------------------------
    // 2x oversampler: linear-phase halfband FIR, windowed sinc, split into
    // its two polyphase branches so nothing is multiplied by a stuffed zero.
    static constexpr int kHalfbandTaps = 63;          // 4*16 - 1
    static constexpr int kPhaseLen = 32;              // taps per branch

    struct Exciter
    {
        Biquad preA, preB;      // 4th-order Butterworth highpass, 6 kHz
        Biquad postA, postB;    // 4th-order Butterworth highpass, 4.5 kHz

        // Rings are written twice (at i and i + kPhaseLen) so a read of
        // kPhaseLen consecutive samples never wraps.
        double upHist[2][2 * kPhaseLen] = {};
        double dnEven[2][2 * kPhaseLen] = {};
        double dnOdd[2][2 * kPhaseLen] = {};
        int pos = 0;            // shared by both channels, advanced once a frame

        void clear();
        void flush();
    };

    //--------------------------------------------------------------------
    void designFilters(int band, int rampLen);
    void glide(int m);
    inline double excite(double x, int ch);

    static inline double harmonics(double v, double drive);

    double fs = 48000.0;

    Settings target;
    double targetDb[kBands] = {0, 0, 0};    // boost dB each band is heading to
    double nowDb[kBands] = {0, 0, 0};       // boost dB each band is at
    bool active[kBands] = {false, false, false};
    bool snapNext = true;

    // Bass
    Svf bassShelf, bassCut;
    // Mids
    Svf midBell, midCut;
    // Highs
    Svf airShelf, presenceCut;
    double airHz = 13000.0;     // shelf corner, solved per rate in prepare()
    Exciter exciter;
    double exDrive = 1.0, exMix = 0.0;

    double hbEven[kPhaseLen] = {};   // h[2j]
    double hbOdd[kPhaseLen] = {};    // h[2j+1]

    // Lamps: power envelopes of each band stage's input and output. The lamp
    // reads the difference of their RMS - how much the band changed the level
    // - which ignores the phase turn a filter gives far outside its region.
    double envIn[kBands] = {0, 0, 0}, envOut[kBands] = {0, 0, 0};
    double envAttack = 0.0, envRelease = 0.0;
    std::atomic<float> lamp[kBands];

    // Glide: one-pole on the dB value, ~20 ms, coefficients recomputed every
    // kSubBlock samples while a value is still moving.
    static constexpr int kSubBlock = 32;
    static constexpr double kGlideMs = 20.0;
};

//------------------------------------------------------------------------
} // namespace Yonie
