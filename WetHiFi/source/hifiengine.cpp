//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//
// WetHiFi's DSP - see hifiengine.h for the signal flow.
//------------------------------------------------------------------------

#include "hifiengine.h"

#include <algorithm>
#include <cmath>

namespace Yonie {

namespace {

constexpr double kPI = 3.14159265358979323846;

// Where each filter sits. The boost and the cut of one knob move together:
// both follow the same glided fraction of the band's maximum.
//
// Bass: the shelf's corner is set above the frequency it is "centred" on,
// with a little resonance (Q above 0.707), so the full boost lands down at
// 30-45 Hz and the shelf rolls back in over the 80-120 Hz region. The bell cut
// a little over an octave and a half above the corner is broad, so the dip
// reads as a scoop rather than a notch.
constexpr double kBassShelfHz = 60.0,  kBassShelfQ = 0.80;
constexpr double kBassCutHz   = 190.0, kBassCutQ   = 0.90;

// Mids: a broad bell (just under one octave and a half wide) at 1 kHz and a
// narrower cut at 3.2 kHz, so the honk is forward and the upper mids do not
// get shouty with it.
constexpr double kMidBellHz = 1000.0, kMidBellQ = 0.90;
constexpr double kMidCutHz  = 3200.0, kMidCutQ  = 1.60;

// Highs: a slow shelf, Q well under 0.707, so it starts low (about 10 kHz is
// where it has given a quarter of its range) and is still rising at 20 kHz -
// the "air" shape rather than a step. A small bell cut at 2.7 kHz takes the
// edge off the harsh region that the extra top would otherwise expose.
//
// The shelf's corner is not a fixed number: the bilinear transform squeezes
// the top octave by a different amount at every rate, so prepare() solves for
// the corner that puts the full boost at exactly kAirAt16kDb at 16 kHz. The
// shelf then measures the same at 44.1 and at 192 kHz where it matters.
constexpr double kAirShelfQ     = 0.50;
constexpr double kAirAt16kDb    = 12.5;
constexpr double kPresenceCutHz  = 2700.0,  kPresenceCutQ  = 1.20;

// Exciter: band above 6 kHz in, harmonics above 4.5 kHz out.
constexpr double kExciteInHz  = 6000.0;
constexpr double kExciteOutHz = 4500.0;
// Butterworth 4th order as two biquads.
constexpr double kButterQ1 = 0.54119610014619698;
constexpr double kButterQ2 = 1.3065629648763766;

// Drive at full knob, and how much of the generated harmonics is mixed back.
// At 10 a -6 dBFS tone at 6 kHz grows a second harmonic about 25 dB under the
// fundamental: sheen, not fuzz.
constexpr double kDriveMax = 6.0;
constexpr double kMixMax   = 0.375;
// The generator is mostly even (a squared, soft-limited term), with a little
// odd from a soft clipper.
constexpr double kEvenAmount = 1.0;
constexpr double kOddAmount  = 0.35;

// Lamps. The lamp reads how far the band moved the level: the difference
// between the RMS going into its stage and the RMS coming out. Reference per
// band: set so pink-noise programme peaking at -6 dBFS with that knob at 10
// lights the lamp to about 0.85. A -6 dBFS tone in the band's own region then
// pins it at 1. Below the reference the lamp falls off over kLampSpanDb
// decibels, which is how a light reads as "a bit" to "a lot".
constexpr double kLampRef[3] = {
    0.12,   // highs
    0.08,   // mids
    0.27,   // bass
};
constexpr double kLampSpanDb = 30.0;
// The follower runs on POWER, so its time constants are half what they are on
// the RMS it stands for: 5 ms attack and 150 ms release on the amplitude. That
// takes a full lamp to dark in about half a second, which reads as the light
// following the music rather than lagging it.
constexpr double kLampAttackMs  = 2.5;
constexpr double kLampReleaseMs = 75.0;

// A band counts as at rest once its glide is this close to the target, and as
// switched off once it is this close to zero.
constexpr double kSnapDb = 1.0e-4;

constexpr double kTiny = 1.0e-30;

inline double flushed(double v) { return std::fabs(v) < kTiny ? 0.0 : v; }

// Modified Bessel function of the first kind, order 0, by its series.
double besselI0(double x)
{
    double sum = 1.0, term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 64; ++k)
    {
        term *= q / (double(k) * double(k));
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

double maxDb(int band)
{
    switch (band)
    {
    case HiFiEngine::kBass:  return HiFiEngine::kBassBoostDb;
    case HiFiEngine::kMids:  return HiFiEngine::kMidBoostDb;
    default:                 return HiFiEngine::kAirBoostDb;
    }
}

} // namespace

//------------------------------------------------------------------------
double HiFiRange::boostFraction(double k)
{
    if (k <= 0.0) return 0.0;
    if (k >= 1.0) return 1.0;
    return k;   // linear in dB (Ronald, 2026-11-20: no curve on the knob)
}

//------------------------------------------------------------------------
// State-variable filter designs: the trapezoidal (bilinear) SVF, whose
// responses are the analogue shelf and bell prototypes prewarped at f0 -
// identical to the cookbook biquads.
//------------------------------------------------------------------------
void HiFiEngine::Svf::flush()
{
    for (int ch = 0; ch < 2; ++ch)
    {
        ic1[ch] = flushed(ic1[ch]);
        ic2[ch] = flushed(ic2[ch]);
    }
}

void HiFiEngine::Svf::setG(double g, double k)
{
    t[kA1] = 1.0 / (1.0 + g * (g + k));
    t[kA2] = g * t[kA1];
    t[kA3] = g * t[kA2];
}

void HiFiEngine::Svf::commit(int rampLen)
{
    if (rampLen <= 1)
    {
        for (int i = 0; i < kN; ++i) c[i] = t[i];
        ramp = 0;
        return;
    }
    for (int i = 0; i < kN; ++i) d[i] = (t[i] - c[i]) / rampLen;
    ramp = rampLen;
}

void HiFiEngine::Svf::lowShelf(double fs, double f0, double q, double dB, int rampLen)
{
    const double A = std::pow(10.0, dB / 40.0);
    const double k = 1.0 / q;
    setG(std::tan(kPI * f0 / fs) / std::sqrt(A), k);
    t[kM0] = 1.0;
    t[kM1] = k * (A - 1.0);
    t[kM2] = A * A - 1.0;
    commit(rampLen);
}

void HiFiEngine::Svf::highShelf(double fs, double f0, double q, double dB, int rampLen)
{
    const double A = std::pow(10.0, dB / 40.0);
    const double k = 1.0 / q;
    setG(std::tan(kPI * f0 / fs) * std::sqrt(A), k);
    t[kM0] = A * A;
    t[kM1] = k * (1.0 - A) * A;
    t[kM2] = 1.0 - A * A;
    commit(rampLen);
}

void HiFiEngine::Svf::bell(double fs, double f0, double q, double dB, int rampLen)
{
    const double A = std::pow(10.0, dB / 40.0);
    const double k = 1.0 / (q * A);
    setG(std::tan(kPI * f0 / fs), k);
    t[kM0] = 1.0;
    t[kM1] = k * (A * A - 1.0);
    t[kM2] = 0.0;
    commit(rampLen);
}

//------------------------------------------------------------------------
// Biquad: the cookbook forms, for the fixed highpasses and for solving the
// air shelf's corner.
//------------------------------------------------------------------------
void HiFiEngine::Biquad::flush()
{
    for (int c = 0; c < 2; ++c)
    {
        s1[c] = flushed(s1[c]);
        s2[c] = flushed(s2[c]);
    }
}

double HiFiEngine::Biquad::magnitude(double fs, double f) const
{
    const double w = 2.0 * kPI * f / fs;
    const double c1 = std::cos(w), s1w = std::sin(w);
    const double c2 = std::cos(2 * w), s2w = std::sin(2 * w);
    const double nr = b0 + b1 * c1 + b2 * c2, ni = -(b1 * s1w + b2 * s2w);
    const double dr = 1.0 + a1 * c1 + a2 * c2, di = -(a1 * s1w + a2 * s2w);
    return std::sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}

void HiFiEngine::Biquad::highShelf(double fs, double f0, double q, double dB)
{
    const double A = std::pow(10.0, dB / 40.0);
    const double w = 2.0 * kPI * f0 / fs;
    const double cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * q);
    const double sA = 2.0 * std::sqrt(A) * alpha;

    const double nb0 = A * ((A + 1) + (A - 1) * cw + sA);
    const double nb1 = -2 * A * ((A - 1) + (A + 1) * cw);
    const double nb2 = A * ((A + 1) + (A - 1) * cw - sA);
    const double na0 = (A + 1) - (A - 1) * cw + sA;
    const double na1 = 2 * ((A - 1) - (A + 1) * cw);
    const double na2 = (A + 1) - (A - 1) * cw - sA;
    b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0;
    a1 = na1 / na0; a2 = na2 / na0;
}

void HiFiEngine::Biquad::highpass(double fs, double f0, double q)
{
    const double w = 2.0 * kPI * f0 / fs;
    const double cw = std::cos(w), sw = std::sin(w);
    const double alpha = sw / (2.0 * q);

    const double na0 = 1 + alpha;
    b0 = ((1 + cw) / 2) / na0;
    b1 = (-(1 + cw)) / na0;
    b2 = ((1 + cw) / 2) / na0;
    a1 = (-2 * cw) / na0;
    a2 = (1 - alpha) / na0;
}

//------------------------------------------------------------------------
void HiFiEngine::Exciter::clear()
{
    preA.clear(); preB.clear(); postA.clear(); postB.clear();
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < 2 * kPhaseLen; ++i)
            upHist[c][i] = dnEven[c][i] = dnOdd[c][i] = 0.0;
    pos = 0;
}

void HiFiEngine::Exciter::flush()
{
    preA.flush(); preB.flush(); postA.flush(); postB.flush();
}

//------------------------------------------------------------------------
HiFiEngine::HiFiEngine()
{
    for (auto& l : lamp) l.store(0.0f, std::memory_order_relaxed);
}

void HiFiEngine::prepare(double sampleRate, int /*maxBlock*/)
{
    fs = sampleRate > 1000.0 ? sampleRate : 48000.0;

    // Halfband lowpass at a quarter of the doubled rate: windowed sinc,
    // Kaiser beta 8 (about 80 dB of stopband), normalised to unity at DC.
    // Every second tap away from the centre is zero, which is what makes it a
    // halfband - and why the odd branch below is nearly empty.
    double h[kHalfbandTaps];
    const int c = (kHalfbandTaps - 1) / 2;
    const double beta = 8.0;
    const double i0b = besselI0(beta);
    double sum = 0.0;
    for (int k = 0; k < kHalfbandTaps; ++k)
    {
        const int m = k - c;
        double s;
        if (m == 0) s = 0.5;
        else if (m % 2 == 0) s = 0.0;
        else s = std::sin(0.5 * kPI * m) / (kPI * m);
        const double r = double(m) / c;
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0b;
        h[k] = s * w;
        sum += h[k];
    }
    for (int k = 0; k < kHalfbandTaps; ++k) h[k] /= sum;
    for (int j = 0; j < kPhaseLen; ++j)
    {
        hbEven[j] = h[2 * j];
        hbOdd[j] = (2 * j + 1 < kHalfbandTaps) ? h[2 * j + 1] : 0.0;
    }

    // The exciter's fixed filters. The pre-filter runs at the base rate.
    exciter.preA.highpass(fs, kExciteInHz, kButterQ1);
    exciter.preB.highpass(fs, kExciteInHz, kButterQ2);
    exciter.postA.highpass(fs, kExciteOutHz, kButterQ1);
    exciter.postB.highpass(fs, kExciteOutHz, kButterQ2);

    // The air shelf's corner: bisect for the one that gives kAirAt16kDb at
    // 16 kHz with the knob at 10. The gain at 16 kHz falls as the corner rises.
    {
        const double fRef = std::min(16000.0, 0.45 * fs);
        double lo = 1000.0, hi = 0.49 * fs;
        Biquad probe;
        for (int it = 0; it < 60; ++it)
        {
            const double mid = 0.5 * (lo + hi);
            probe.highShelf(fs, mid, kAirShelfQ, kAirBoostDb);
            const double g = 20.0 * std::log10(probe.magnitude(fs, fRef));
            if (g > kAirAt16kDb) lo = mid; else hi = mid;
        }
        airHz = 0.5 * (lo + hi);
    }

    // Every EQ filter gets its coefficients now, at whatever gain it holds,
    // so it is running and tracking the signal from the first sample.
    for (int b = 0; b < kBands; ++b) designFilters(b, 0);

    envAttack = 1.0 - std::exp(-1.0 / (fs * kLampAttackMs * 0.001));
    envRelease = 1.0 - std::exp(-1.0 / (fs * kLampReleaseMs * 0.001));

    reset();
}

void HiFiEngine::reset()
{
    bassShelf.clear(); bassCut.clear();
    midBell.clear(); midCut.clear();
    airShelf.clear(); presenceCut.clear();
    exciter.clear();
    for (int b = 0; b < kBands; ++b)
    {
        envIn[b] = envOut[b] = 0.0;
        lamp[b].store(0.0f, std::memory_order_relaxed);
    }
    // The next block jumps straight to the settings rather than gliding in
    // from wherever the knobs were: a fresh insert or a transport reset should
    // not fade its EQ in.
    snapNext = true;
}

void HiFiEngine::setSettings(const Settings& s)
{
    auto clampStep = [](int v) {
        return v < 0 ? 0 : (v > HiFiRange::kSteps - 1 ? HiFiRange::kSteps - 1 : v);
    };
    target.highs = clampStep(s.highs);
    target.mids = clampStep(s.mids);
    target.bass = clampStep(s.bass);

    const int steps[kBands] = { target.highs, target.mids, target.bass };
    for (int b = 0; b < kBands; ++b)
        targetDb[b] = maxDb(b) *
            HiFiRange::boostFraction(double(steps[b]) / (HiFiRange::kSteps - 1));
}

float HiFiEngine::lampLevel(int band) const
{
    if (band < 0 || band >= kBands) return 0.0f;
    return lamp[band].load(std::memory_order_relaxed);
}

//------------------------------------------------------------------------
// Coefficients for one band at its current (glided) boost. The cut follows
// the boost as the same fraction of its own maximum.
void HiFiEngine::designFilters(int band, int rampLen)
{
    const double frac = nowDb[band] / maxDb(band);
    switch (band)
    {
    case kBass:
        bassShelf.lowShelf(fs, kBassShelfHz, kBassShelfQ, nowDb[band], rampLen);
        bassCut.bell(fs, kBassCutHz, kBassCutQ, kBassCutDb * frac, rampLen);
        break;
    case kMids:
        midBell.bell(fs, kMidBellHz, kMidBellQ, nowDb[band], rampLen);
        midCut.bell(fs, kMidCutHz, kMidCutQ, kMidCutDb * frac, rampLen);
        break;
    default:
    {
        airShelf.highShelf(fs, airHz, kAirShelfQ, nowDb[band], rampLen);
        presenceCut.bell(fs, kPresenceCutHz, kPresenceCutQ, kPresenceCutDb * frac, rampLen);
        // The exciter follows the knob POSITION, recovered from the glided
        // boost, so its drive glides with everything else.
        const double k = std::pow(std::max(0.0, frac), 1.0 / 1.2);
        exDrive = 1.0 + (kDriveMax - 1.0) * k;
        exMix = kMixMax * k;
        break;
    }
    }
}

// Move every band's boost one step along its glide, and refresh the
// coefficients of any band that is still moving. m is the sub-block length.
void HiFiEngine::glide(int m)
{
    const bool snap = snapNext;
    const double a = snap ? 1.0
                          : 1.0 - std::exp(-double(m) / (fs * kGlideMs * 0.001));
    snapNext = false;

    for (int b = 0; b < kBands; ++b)
    {
        if (nowDb[b] == targetDb[b] && (active[b] == (nowDb[b] != 0.0)))
            continue;

        double next = nowDb[b] + a * (targetDb[b] - nowDb[b]);
        if (std::fabs(targetDb[b] - next) < kSnapDb) next = targetDb[b];
        nowDb[b] = next;

        // The EQ filters never stop running, so their integrators always hold
        // the state of the signal going through them and a knob leaving 0
        // starts from there - no wake-up transient. At exactly 0 dB every one
        // of them has m0 = 1 and m1 = m2 = 0, which passes the input through
        // bit for bit.
        //
        // Only the exciter is switched off at 0. It starts from empty state
        // when it wakes, and its mix is a hair above zero at that moment.
        const bool on = nowDb[b] != 0.0;
        if (b == kHighs && on && !active[b])
            exciter.clear();
        active[b] = on;
        designFilters(b, snap ? 0 : m);
    }
}

//------------------------------------------------------------------------
// The harmonic generator. Returns the distortion of a soft amplifier stage
// and NOT its output - the input is subtracted - so only new partials come
// out of it.
//
//   even:  u^2 / (1 + u^2)     ~ u^2 when small, so a clean 2nd harmonic,
//                              soft-limited so a loud transient cannot spike
//   odd:   tanh(u) - u         ~ -u^3/3 when small, a little 3rd harmonic
//
// divided by the drive again, so turning the drive up raises the harmonics
// relative to the signal and not the overall level.
inline double HiFiEngine::harmonics(double v, double drive)
{
    const double u = drive * v;
    const double u2 = u * u;
    const double even = u2 / (1.0 + u2);
    const double odd = std::tanh(u) - u;
    return (kEvenAmount * even + kOddAmount * odd) / drive;
}

inline double HiFiEngine::excite(double x, int ch)
{
    Exciter& e = exciter;
    double band = e.preB.run(e.preA.run(x, ch), ch);

    const int p = e.pos;   // newest slot; reads walk forward from it into older ones

    // Up 2x. The stuffed stream is 2x[n], 0, so each output phase is one
    // branch of the halfband against the input history.
    e.upHist[ch][p] = band;
    e.upHist[ch][p + kPhaseLen] = band;
    const double* hx = &e.upHist[ch][p];
    double v0 = 0.0, v1 = 0.0;
    for (int j = 0; j < kPhaseLen; ++j)
    {
        v0 += hbEven[j] * hx[j];
        v1 += hbOdd[j] * hx[j];
    }
    v0 *= 2.0;
    v1 *= 2.0;

    // The nonlinearity, at the doubled rate.
    const double g0 = harmonics(v0, exDrive);
    const double g1 = harmonics(v1, exDrive);

    // Down 2x: the halfband again, evaluated only at the samples we keep.
    e.dnEven[ch][p] = g0;
    e.dnEven[ch][p + kPhaseLen] = g0;
    e.dnOdd[ch][p] = g1;
    e.dnOdd[ch][p + kPhaseLen] = g1;
    const double* de = &e.dnEven[ch][p];
    const double* dd = &e.dnOdd[ch][p];
    double y = 0.0;
    for (int j = 0; j < kPhaseLen; ++j)
        y += hbEven[j] * dd[j] + hbOdd[j] * de[j];

    // DC and the low difference tones the squaring makes go here.
    y = e.postB.run(e.postA.run(y, ch), ch);
    return y * exMix;
}

//------------------------------------------------------------------------
void HiFiEngine::processStereo(const float* inL, const float* inR,
                               float* outL, float* outR, int n)
{
    if (n <= 0) return;

    for (int start = 0; start < n; start += kSubBlock)
    {
        const int m = std::min(kSubBlock, n - start);
        glide(m);

        const bool exciteOn = active[kHighs];

        auto follow = [this](double& e, double pw) {
            e += (pw > e ? envAttack : envRelease) * (pw - e);
        };

        for (int i = start; i < start + m; ++i)
        {
            const double x[2] = { double(inL[i]), double(inR[i]) };
            double y[2];
            double pIn[kBands] = {}, pOut[kBands] = {};

            for (int ch = 0; ch < 2; ++ch)
            {
                double s = x[ch];
                {
                    const double t = bassCut.run(bassShelf.run(s, ch), ch);
                    pIn[kBass] += s * s;
                    pOut[kBass] += t * t;
                    s = t;
                }
                {
                    const double t = midCut.run(midBell.run(s, ch), ch);
                    pIn[kMids] += s * s;
                    pOut[kMids] += t * t;
                    s = t;
                }
                {
                    double t = presenceCut.run(airShelf.run(s, ch), ch);
                    if (exciteOn)
                        t += excite(x[ch], ch);
                    pIn[kHighs] += s * s;
                    pOut[kHighs] += t * t;
                    s = t;
                }
                y[ch] = s;
            }
            bassShelf.advance(); bassCut.advance();
            midBell.advance(); midCut.advance();
            airShelf.advance(); presenceCut.advance();
            if (exciteOn)
                exciter.pos = (exciter.pos + kPhaseLen - 1) % kPhaseLen;

            outL[i] = float(y[0]);
            outR[i] = float(y[1]);

            for (int b = 0; b < kBands; ++b)
            {
                follow(envIn[b], 0.5 * pIn[b]);
                follow(envOut[b], 0.5 * pOut[b]);
            }
        }

        bassShelf.flush(); bassCut.flush();
        midBell.flush(); midCut.flush();
        airShelf.flush(); presenceCut.flush();
        exciter.flush();
        for (int b = 0; b < kBands; ++b)
        {
            envIn[b] = flushed(envIn[b]);
            envOut[b] = flushed(envOut[b]);
        }
    }

    // What the band did to the level: the difference of the two RMS
    // envelopes, a boost or a cut alike, against the band's reference.
    for (int b = 0; b < kBands; ++b)
    {
        float v = 0.0f;
        const double d = std::fabs(std::sqrt(envOut[b]) - std::sqrt(envIn[b]));
        if (d > 1e-10)
        {
            const double db = 20.0 * std::log10(d / kLampRef[b]);
            v = float(std::min(1.0, std::max(0.0, (db + kLampSpanDb) / kLampSpanDb)));
        }
        lamp[b].store(v, std::memory_order_relaxed);
    }
}

//------------------------------------------------------------------------
} // namespace Yonie
