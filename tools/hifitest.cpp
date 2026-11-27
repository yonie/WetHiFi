//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//
// hifitest - measures HiFiEngine directly, with no plugin and no host.
//
// Compiles against the same source the plugin uses, so a constant that does
// not do what its name says shows up here in a second rather than after a full
// VST3 build and a hand test in a DAW.
//
// What it measures:
//   * the null: all knobs at 0 is the input, bit for bit, also after a knob
//     has been up and come back down;
//   * each knob at 10 alone, measured with sine tones at the key frequencies,
//     so the boost and the cut are where the header says;
//   * the dip above the bass bump;
//   * the exciter growing 2nd and 3rd harmonics with the HIGHS knob, and its
//     oversampling keeping the aliases down;
//   * no NaN, no inf, no runaway at 44.1, 48, 96 and 192 kHz under white
//     noise at 0 dBFS with the knobs automated at random;
//   * in-place processing giving identical output;
//   * no click on a knob move;
//   * rescaleStep on the 11- and 41-step grids;
//   * the lamps.
//
// Build and run (from the plugin repo root):  tools\build-hifitest.bat
//------------------------------------------------------------------------

#include "hifiengine.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

using namespace Yonie;

namespace {

constexpr double kPI = 3.14159265358979323846;
constexpr double kRate = 48000.0;
constexpr int kMax = HiFiRange::kSteps - 1;

int failures = 0;
int passes = 0;

double dB(double lin) { return 20.0 * std::log10(lin > 1e-15 ? lin : 1e-15); }

void check(bool ok, const char* what, const char* detail = "")
{
    std::printf("  [%s] %s%s%s\n", ok ? "PASS" : "FAIL", what,
                detail[0] ? "  " : "", detail);
    if (ok) ++passes; else ++failures;
}

HiFiEngine::Settings make(int highs, int mids, int bass)
{
    HiFiEngine::Settings s;
    s.highs = highs; s.mids = mids; s.bass = bass;
    return s;
}

struct Rng
{
    uint32_t st;
    explicit Rng(uint32_t seed) : st(seed) {}
    uint32_t next() { st ^= st << 13; st ^= st >> 17; st ^= st << 5; return st; }
    double uni() { return (next() >> 8) * (1.0 / 16777216.0); }        // 0..1
    double bip() { return 2.0 * uni() - 1.0; }                          // -1..1
    int below(int n) { return int(next() % uint32_t(n)); }
};

std::vector<float> sine(double freq, int n, double amp, double fs = kRate)
{
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        x[size_t(i)] = float(amp * std::sin(2.0 * kPI * freq * i / fs));
    return x;
}

std::vector<float> noise(int n, double amp, uint32_t seed)
{
    Rng r(seed);
    std::vector<float> x(static_cast<size_t>(n));
    for (auto& v : x) v = float(amp * r.bip());
    return x;
}

// Pink-ish noise (the usual three-pole approximation), scaled to a peak.
std::vector<float> pink(int n, double peak, uint32_t seed)
{
    Rng r(seed);
    std::vector<double> y(static_cast<size_t>(n));
    double b0 = 0, b1 = 0, b2 = 0, mx = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = r.bip();
        b0 = 0.99765 * b0 + w * 0.0990460;
        b1 = 0.96300 * b1 + w * 0.2965164;
        b2 = 0.57000 * b2 + w * 1.0526913;
        y[size_t(i)] = b0 + b1 + b2 + w * 0.1848;
        mx = std::max(mx, std::fabs(y[size_t(i)]));
    }
    std::vector<float> x(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) x[size_t(i)] = float(y[size_t(i)] * peak / mx);
    return x;
}

// Run the engine over a mono signal (same on both channels), block by block.
void run(HiFiEngine& e, const std::vector<float>& in, std::vector<float>& outL,
         std::vector<float>* outR = nullptr, int block = 512)
{
    const int n = int(in.size());
    outL.assign(size_t(n), 0.0f);
    std::vector<float> r(static_cast<size_t>(n));
    for (int i = 0; i < n; i += block)
    {
        const int m = std::min(block, n - i);
        e.processStereo(in.data() + i, in.data() + i, outL.data() + i, r.data() + i, m);
    }
    if (outR) *outR = r;
}

// Amplitude of the component at f over [from, from+len). Pick len so f has a
// whole number of cycles in it and every other partial is exactly orthogonal.
double amp(const std::vector<float>& x, int from, int len, double f, double fs = kRate)
{
    double re = 0.0, im = 0.0;
    for (int i = 0; i < len; ++i)
    {
        const double ph = 2.0 * kPI * f * i / fs;
        re += x[size_t(from + i)] * std::cos(ph);
        im += x[size_t(from + i)] * std::sin(ph);
    }
    return 2.0 * std::hypot(re, im) / len;
}

// Gain at f, in dB, through the engine at these settings. A quiet tone, so the
// exciter's nonlinearity has nothing to say about the fundamental. One second
// to settle, then exactly one second measured: every whole-hertz frequency has
// a whole number of cycles in it.
double gainAt(const HiFiEngine::Settings& s, double f)
{
    HiFiEngine e;
    e.prepare(kRate, 512);
    e.setSettings(s);
    e.reset();
    const int n = int(2 * kRate);
    const double a = 0.05;
    auto in = sine(f, n, a);
    std::vector<float> out;
    run(e, in, out);
    return dB(amp(out, int(kRate), int(kRate), f) / amp(in, int(kRate), int(kRate), f));
}

//------------------------------------------------------------------------
void testNull()
{
    std::printf("\nNull at all knobs 0\n");
    HiFiEngine e;
    e.prepare(kRate, 512);
    e.setSettings(make(0, 0, 0));
    e.reset();
    auto in = noise(int(kRate * 2), 1.0, 0xC0FFEEu);
    std::vector<float> out;
    run(e, in, out);
    double mx = 0.0;
    for (size_t i = 0; i < in.size(); ++i) mx = std::max(mx, double(std::fabs(out[i] - in[i])));
    char buf[128];
    std::snprintf(buf, sizeof buf, "max |diff| = %.3g (%s)", mx, mx == 0.0 ? "bit-exact" : "not exact");
    check(mx == 0.0, "all-zero output equals input", buf);

    // Up to 10 on every knob, then back to 0: after the glide has finished the
    // filters must be bypassed again, not merely flat.
    e.setSettings(make(kMax, kMax, kMax));
    run(e, in, out);
    e.setSettings(make(0, 0, 0));
    run(e, in, out);   // two seconds of glide back down
    run(e, in, out);
    mx = 0.0;
    for (size_t i = 0; i < in.size(); ++i) mx = std::max(mx, double(std::fabs(out[i] - in[i])));
    std::snprintf(buf, sizeof buf, "max |diff| = %.3g", mx);
    check(mx == 0.0, "after 10 -> 0, output is the input again", buf);
}

//------------------------------------------------------------------------
const double kFreqs[] = { 30, 45, 100, 180, 500, 1000, 2700, 3200, 8000, 12000, 16000, 20000 };
constexpr int kNF = int(sizeof kFreqs / sizeof kFreqs[0]);

int idx(double f)
{
    for (int i = 0; i < kNF; ++i) if (kFreqs[i] == f) return i;
    return 0;
}

void printRow(const char* name, const double* g)
{
    std::printf("  %-12s", name);
    for (int i = 0; i < kNF; ++i) std::printf("%7.2f", g[i]);
    std::printf("\n");
}

void testResponse()
{
    std::printf("\nMagnitude response, each knob at 10 alone, 48 kHz (dB)\n");
    std::printf("  %-12s", "Hz");
    for (int i = 0; i < kNF; ++i) std::printf("%7.0f", kFreqs[i]);
    std::printf("\n");

    double gb[kNF], gm[kNF], gh[kNF];
    for (int i = 0; i < kNF; ++i)
    {
        gb[i] = gainAt(make(0, 0, kMax), kFreqs[i]);
        gm[i] = gainAt(make(0, kMax, 0), kFreqs[i]);
        gh[i] = gainAt(make(kMax, 0, 0), kFreqs[i]);
    }
    printRow("TIGHT BASS", gb);
    printRow("FOCUSED MIDS", gm);
    printRow("SILKY HIGHS", gh);

    char buf[160];
    // Bass
    std::snprintf(buf, sizeof buf, "30 Hz %+.2f, 45 Hz %+.2f dB", gb[idx(30)], gb[idx(45)]);
    check(gb[idx(30)] > 12.5 && gb[idx(30)] < 16.5 && gb[idx(45)] > 9.0, "bass: deep boost at 30-45 Hz", buf);
    std::snprintf(buf, sizeof buf, "180 Hz %+.2f dB", gb[idx(180)]);
    check(gb[idx(180)] < -3.0 && gb[idx(180)] > -7.0, "bass: cut around 180 Hz", buf);
    std::snprintf(buf, sizeof buf, "500 Hz %+.2f, 1 kHz %+.2f, 16 kHz %+.2f dB",
                  gb[idx(500)], gb[idx(1000)], gb[idx(16000)]);
    check(std::fabs(gb[idx(500)]) < 2.5 && std::fabs(gb[idx(1000)]) < 0.8 &&
          std::fabs(gb[idx(16000)]) < 0.1, "bass: leaves mids and highs alone", buf);

    // The dip above the bump: scan finely between 100 and 400 Hz.
    double dipF = 0, dipG = 1e9;
    for (double f = 100; f <= 400; f += 10)
    {
        const double g = gainAt(make(0, 0, kMax), f);
        if (g < dipG) { dipG = g; dipF = f; }
    }
    std::snprintf(buf, sizeof buf, "deepest %+.2f dB at %.0f Hz, bump %+.2f dB at 45 Hz",
                  dipG, dipF, gb[idx(45)]);
    check(dipG < -2.0 && dipF >= 140 && dipF <= 260 && gb[idx(45)] - dipG > 10.0,
          "bass: dip sits above the bump", buf);

    // Mids
    std::snprintf(buf, sizeof buf, "1 kHz %+.2f dB", gm[idx(1000)]);
    check(gm[idx(1000)] > 9.0 && gm[idx(1000)] < 11.0, "mids: boost at 1 kHz", buf);
    std::snprintf(buf, sizeof buf, "3.2 kHz %+.2f dB", gm[idx(3200)]);
    check(gm[idx(3200)] < -0.5 && gm[idx(3200)] > -4.0, "mids: cut at 3.2 kHz", buf);
    std::snprintf(buf, sizeof buf, "30 Hz %+.2f, 100 Hz %+.2f, 20 kHz %+.2f dB",
                  gm[idx(30)], gm[idx(100)], gm[idx(20000)]);
    check(std::fabs(gm[idx(30)]) < 0.3 && std::fabs(gm[idx(100)]) < 1.0 &&
          std::fabs(gm[idx(20000)]) < 0.5, "mids: leaves lows and air alone", buf);

    // Highs
    std::snprintf(buf, sizeof buf, "16 kHz %+.2f dB, 20 kHz %+.2f dB", gh[idx(16000)], gh[idx(20000)]);
    check(gh[idx(16000)] > 11.5 && gh[idx(16000)] < 13.5 && gh[idx(20000)] > gh[idx(16000)],
          "highs: ~+12.5 dB at 16 kHz, still rising at 20 kHz", buf);
    std::snprintf(buf, sizeof buf, "8 kHz %+.2f, 12 kHz %+.2f dB", gh[idx(8000)], gh[idx(12000)]);
    check(gh[idx(8000)] > 1.0 && gh[idx(8000)] < gh[idx(12000)] && gh[idx(12000)] < gh[idx(16000)],
          "highs: shelf rises from ~10 kHz", buf);
    std::snprintf(buf, sizeof buf, "2.7 kHz %+.2f dB", gh[idx(2700)]);
    check(gh[idx(2700)] < -1.0 && gh[idx(2700)] > -3.0, "highs: presence cut near 2.7 kHz", buf);
    std::snprintf(buf, sizeof buf, "30 Hz %+.2f, 100 Hz %+.2f, 500 Hz %+.2f dB",
                  gh[idx(30)], gh[idx(100)], gh[idx(500)]);
    check(std::fabs(gh[idx(30)]) < 0.1 && std::fabs(gh[idx(100)]) < 0.1 &&
          std::fabs(gh[idx(500)]) < 0.5, "highs: leaves the lows alone", buf);

    // The taper: straight in dB, so half way is half the boost.
    const double half = gainAt(make(0, 0, kMax / 2), 30);
    std::snprintf(buf, sizeof buf, "bass at 5: %+.2f dB at 30 Hz against %+.2f at 10", half, gb[idx(30)]);
    check(half > 0.45 * gb[idx(30)] && half < 0.55 * gb[idx(30)], "taper: 5 is half of 10 in dB", buf);
}

//------------------------------------------------------------------------
void testExciter()
{
    std::printf("\nExciter: 6 kHz at -6 dBFS, harmonics against the output fundamental\n");
    const int n = int(2 * kRate);
    auto in = sine(6000, n, 0.5);
    double h2[5], h3[5];
    const int knobs[5] = { 0, 10, 20, 30, 40 };
    for (int k = 0; k < 5; ++k)
    {
        HiFiEngine e;
        e.prepare(kRate, 512);
        e.setSettings(make(knobs[k], 0, 0));
        e.reset();
        std::vector<float> out;
        run(e, in, out);
        const int from = int(kRate), len = int(kRate);
        const double f1 = amp(out, from, len, 6000);
        h2[k] = dB(amp(out, from, len, 12000) / f1);
        h3[k] = dB(amp(out, from, len, 18000) / f1);
        std::printf("    highs %4.1f   H2 %7.1f dBc   H3 %7.1f dBc\n", knobs[k] / 4.0, h2[k], h3[k]);
    }
    char buf[160];
    std::snprintf(buf, sizeof buf, "H2 %.1f -> %.1f dBc", h2[1], h2[4]);
    check(h2[0] < -120 && h2[1] < h2[2] && h2[2] < h2[3] && h2[3] < h2[4] + 0.5,
          "2nd harmonic grows with the knob", buf);
    std::snprintf(buf, sizeof buf, "H3 %.1f -> %.1f dBc", h3[1], h3[4]);
    check(h3[0] < -120 && h3[1] < h3[2] && h3[2] < h3[3] && h3[3] < h3[4],
          "3rd harmonic grows with the knob", buf);
    std::snprintf(buf, sizeof buf, "at 10: H2 %.1f dBc, H3 %.1f dBc", h2[4], h3[4]);
    check(h2[4] > -35 && h2[4] < -15 && h3[4] < h2[4], "sheen, not fuzz: mostly 2nd, well under the tone", buf);

    // Aliasing: 15 kHz in. Its 2nd harmonic (30 kHz) would fold to 18 kHz and
    // its 3rd (45 kHz) to 3 kHz without oversampling.
    {
        HiFiEngine e;
        e.prepare(kRate, 512);
        e.setSettings(make(kMax, 0, 0));
        e.reset();
        auto x = sine(15000, n, 0.5);
        std::vector<float> out;
        run(e, x, out);
        const int from = int(kRate), len = int(kRate);
        const double f1 = amp(out, from, len, 15000);
        const double a18 = dB(amp(out, from, len, 18000) / f1);
        const double a3 = dB(amp(out, from, len, 3000) / f1);
        std::snprintf(buf, sizeof buf, "fold of H2 at 18 kHz %.1f dBc, fold of H3 at 3 kHz %.1f dBc", a18, a3);
        check(a18 < -60 && a3 < -80, "oversampling keeps the aliases down", buf);
    }
}

//------------------------------------------------------------------------
void testStability()
{
    std::printf("\nStability: white noise at 0 dBFS, random knob automation, 10 s\n");
    const double rates[] = { 44100, 48000, 96000, 192000 };
    for (double fs : rates)
    {
        HiFiEngine e;
        e.prepare(fs, 4096);
        Rng r(0x51A7u + uint32_t(fs));
        const int n = int(fs * 10);
        std::vector<float> L(4096), R(4096), oL(4096), oR(4096);
        bool finite = true;
        double peak = 0.0;
        int done = 0;
        int nextKnob = 0;
        while (done < n)
        {
            const int m = std::min(n - done, 1 + r.below(4096));
            for (int i = 0; i < m; ++i)
            {
                L[size_t(i)] = float(r.bip());
                R[size_t(i)] = float(r.bip());
            }
            if (done >= nextKnob)
            {
                e.setSettings(make(r.below(41), r.below(41), r.below(41)));
                nextKnob = done + int(fs * (0.005 + 0.1 * r.uni()));
            }
            e.processStereo(L.data(), R.data(), oL.data(), oR.data(), m);
            for (int i = 0; i < m; ++i)
            {
                if (!std::isfinite(oL[size_t(i)]) || !std::isfinite(oR[size_t(i)])) finite = false;
                peak = std::max(peak, double(std::max(std::fabs(oL[size_t(i)]), std::fabs(oR[size_t(i)]))));
            }
            for (int b = 0; b < 3; ++b)
                if (!std::isfinite(e.lampLevel(b))) finite = false;
            done += m;
        }
        char what[96], buf[96];
        std::snprintf(what, sizeof what, "%.1f kHz: finite and bounded", fs / 1000.0);
        std::snprintf(buf, sizeof buf, "peak %.2f (%+.1f dBFS)", peak, dB(peak));
        check(finite && peak < 12.0, what, buf);
    }

    // And settled response at every rate: the boosts land in the same place.
    std::printf("  settled response at other rates (knob at 10):\n");
    for (double fs : rates)
    {
        auto g = [&](const HiFiEngine::Settings& s, double f) {
            HiFiEngine e;
            e.prepare(fs, 512);
            e.setSettings(s);
            e.reset();
            const int len = int(fs);
            auto in = sine(f, 2 * len, 0.05, fs);
            std::vector<float> out;
            run(e, in, out);
            return dB(amp(out, len, len, f, fs) / amp(in, len, len, f, fs));
        };
        std::printf("    %6.1f kHz: bass 45 Hz %+.2f, 180 Hz %+.2f | mids 1k %+.2f, 3.2k %+.2f | highs 16k %+.2f, 2.7k %+.2f\n",
                    fs / 1000.0, g(make(0, 0, kMax), 45), g(make(0, 0, kMax), 180),
                    g(make(0, kMax, 0), 1000), g(make(0, kMax, 0), 3200),
                    g(make(kMax, 0, 0), 16000), g(make(kMax, 0, 0), 2700));
    }
}

//------------------------------------------------------------------------
void testInPlace()
{
    std::printf("\nIn-place processing\n");
    const int n = int(kRate * 3);
    auto inL = noise(n, 0.7, 11u), inR = noise(n, 0.7, 22u);
    HiFiEngine a, b;
    a.prepare(kRate, 1024);
    b.prepare(kRate, 1024);
    std::vector<float> oL(static_cast<size_t>(n)), oR(static_cast<size_t>(n));
    std::vector<float> pL = inL, pR = inR;
    Rng r(99u);
    int done = 0;
    while (done < n)
    {
        const int m = std::min(n - done, 1 + r.below(1024));
        const auto s = make(r.below(41), r.below(41), r.below(41));
        a.setSettings(s);
        b.setSettings(s);
        a.processStereo(inL.data() + done, inR.data() + done, oL.data() + done, oR.data() + done, m);
        b.processStereo(pL.data() + done, pR.data() + done, pL.data() + done, pR.data() + done, m);
        done += m;
    }
    bool same = true;
    for (int i = 0; i < n; ++i)
        if (oL[size_t(i)] != pL[size_t(i)] || oR[size_t(i)] != pR[size_t(i)]) { same = false; break; }
    check(same, "in-place output is identical, sample for sample");

    bool lr = true;
    {
        HiFiEngine e;
        e.prepare(kRate, 512);
        e.setSettings(make(kMax, kMax, kMax));
        e.reset();
        auto x = noise(n, 0.5, 5u);
        std::vector<float> l, rr;
        run(e, x, l, &rr);
        for (int i = 0; i < n; ++i) if (l[size_t(i)] != rr[size_t(i)]) { lr = false; break; }
    }
    check(lr, "left and right run identical filters");
}

//------------------------------------------------------------------------
// The largest step between neighbouring samples, over a range.
double maxJump(const std::vector<float>& y, int from, int to, int* at = nullptr)
{
    double m = 0.0;
    for (int i = std::max(1, from); i < to; ++i)
    {
        const double d = std::fabs(double(y[size_t(i)]) - y[size_t(i - 1)]);
        if (d > m) { m = d; if (at) *at = i; }
    }
    return m;
}

void testClicks()
{
    std::printf("\nKnob moves: no click\n");
    struct Case { const char* name; int band; double f; };
    const Case cases[] = { { "bass", 2, 50.0 }, { "mids", 1, 1000.0 }, { "highs", 0, 100.0 },
                           { "highs", 0, 3000.0 } };
    for (const Case& c : cases)
    {
        auto setBand = [&](int v) {
            HiFiEngine::Settings s;
            if (c.band == 0) s.highs = v; else if (c.band == 1) s.mids = v; else s.bass = v;
            return s;
        };
        HiFiEngine e;
        e.prepare(kRate, 256);
        e.setSettings(setBand(0));
        e.reset();
        const int n = int(kRate * 4);
        auto in = sine(c.f, n, 0.2);
        std::vector<float> out(static_cast<size_t>(n));
        const int block = 256;
        for (int i = 0; i < n; i += block)
        {
            const double t = i / kRate;
            // 0-0.5 s at 0; jump to 10 at once; 1.5 s jump back to 0;
            // 2.5 s a fine sweep up, one step per block.
            int v = 0;
            if (t >= 0.5 && t < 1.5) v = kMax;
            else if (t >= 2.5) v = std::min(kMax, int((t - 2.5) * kRate / block));
            e.setSettings(setBand(v));
            e.processStereo(in.data() + i, in.data() + i, out.data() + i, out.data() + i, std::min(block, n - i));
        }
        // Natural slope at the two settled ends.
        const double atZero = maxJump(out, int(0.2 * kRate), int(0.5 * kRate));
        const double atTen = maxJump(out, int(1.2 * kRate), int(1.5 * kRate));
        const double natural = std::max(atZero, atTen);
        int at = 0;
        const double moving = maxJump(out, int(0.5 * kRate), n, &at);
        char what[96], buf[128];
        std::snprintf(what, sizeof what, "%s knob on a %.0f Hz sine", c.name, c.f);
        std::snprintf(buf, sizeof buf, "max jump %.5f while moving (at %.3f s), %.5f settled (x%.3f)",
                      moving, at / kRate, natural, moving / natural);
        check(moving < natural * 1.15, what, buf);
    }
}

//------------------------------------------------------------------------
void testRescale()
{
    std::printf("\nrescaleStep\n");
    bool ok11 = true;
    for (int v = 0; v < 11; ++v)
        if (HiFiRange::rescaleStep(v, 11) != v * 4) ok11 = false;
    check(ok11, "11-step grid: detent v lands on fine step 4v");
    bool ok41 = true;
    for (int v = 0; v < 41; ++v)
        if (HiFiRange::rescaleStep(v, 41) != v) ok41 = false;
    check(ok41, "41-step grid: identity");
    char buf[128];
    const int a = HiFiRange::rescaleStep(-3, 11), b = HiFiRange::rescaleStep(99, 11),
              c = HiFiRange::rescaleStep(99, 41), d = HiFiRange::rescaleStep(5, 1);
    std::snprintf(buf, sizeof buf, "-3/11 -> %d, 99/11 -> %d, 99/41 -> %d, 5/1 -> %d", a, b, c, d);
    check(a == 0 && b == 40 && c == 40 && d == 0, "out of range is clamped", buf);
    const int e = HiFiRange::rescaleStep(1, 21), f = HiFiRange::rescaleStep(1, 81),
              g = HiFiRange::rescaleStep(3, 81);
    std::snprintf(buf, sizeof buf, "1/21 -> %d, 1/81 -> %d, 3/81 -> %d", e, f, g);
    check(e == 2 && f == 1 && g == 2, "other grids round to nearest", buf);
}

//------------------------------------------------------------------------
// Lamp levels after a second of a signal at these settings.
void lampsAfter(const HiFiEngine::Settings& s, const std::vector<float>& in, float out[3])
{
    HiFiEngine e;
    e.prepare(kRate, 512);
    e.setSettings(s);
    e.reset();
    std::vector<float> y;
    run(e, in, y);
    for (int b = 0; b < 3; ++b) out[b] = e.lampLevel(b);
}

void testLamps()
{
    std::printf("\nLamps (0 highs, 1 mids, 2 bass)\n");
    const int n = int(kRate);
    float l[3];
    char buf[160];

    lampsAfter(make(kMax, kMax, kMax), std::vector<float>(size_t(n), 0.0f), l);
    std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f", l[0], l[1], l[2]);
    check(l[0] == 0 && l[1] == 0 && l[2] == 0, "silence: all dark", buf);

    lampsAfter(make(0, 0, 0), noise(n, 1.0, 3u), l);
    std::snprintf(buf, sizeof buf, "%.3f %.3f %.3f", l[0], l[1], l[2]);
    check(l[0] == 0 && l[1] == 0 && l[2] == 0, "knobs at 0, loud noise: all dark", buf);

    lampsAfter(make(0, 0, 1), sine(50, n, 0.5), l);
    std::snprintf(buf, sizeof buf, "bass %.3f", l[2]);
    check(l[2] < 0.35, "bass knob one fine step up: lamp low", buf);

    struct Tone { const char* name; double f; int band; };
    const Tone tones[] = { { "50 Hz kick", 50, 2 }, { "1 kHz", 1000, 1 }, { "14 kHz", 14000, 0 } };
    for (const Tone& t : tones)
    {
        lampsAfter(make(kMax, kMax, kMax), sine(t.f, n, 0.5), l);
        float others = 0;
        for (int b = 0; b < 3; ++b) if (b != t.band) others = std::max(others, l[b]);
        std::snprintf(buf, sizeof buf, "highs %.3f  mids %.3f  bass %.3f", l[0], l[1], l[2]);
        char what[96];
        std::snprintf(what, sizeof what, "%s at -6 dBFS, all at 10: its own lamp leads", t.name);
        check(l[t.band] > 0.85 && others < l[t.band] - 0.2, what, buf);
    }

    // Programme: pink noise peaking at -6 dBFS, each knob at 10 alone.
    auto p = pink(int(kRate * 2), 0.5, 77u);
    float pl[3];
    lampsAfter(make(kMax, 0, 0), p, l); pl[0] = l[0];
    lampsAfter(make(0, kMax, 0), p, l); pl[1] = l[1];
    lampsAfter(make(0, 0, kMax), p, l); pl[2] = l[2];
    std::snprintf(buf, sizeof buf, "highs %.3f  mids %.3f  bass %.3f", pl[0], pl[1], pl[2]);
    check(pl[0] > 0.75 && pl[1] > 0.75 && pl[2] > 0.75, "pink programme peaking -6 dBFS, knob at 10: lamp 0.8-1.0", buf);

    // Release: after the signal stops the lamp falls, over a fraction of a second.
    {
        HiFiEngine e;
        e.prepare(kRate, 512);
        e.setSettings(make(0, 0, kMax));
        e.reset();
        std::vector<float> y;
        run(e, sine(50, n, 0.1), y);   // -20 dBFS, so the lamp is not pinned at 1
        const float lit = e.lampLevel(2);
        run(e, std::vector<float>(size_t(kRate * 0.1), 0.0f), y);
        const float after100 = e.lampLevel(2);
        run(e, std::vector<float>(size_t(kRate * 1.0), 0.0f), y);
        const float after1s = e.lampLevel(2);
        std::snprintf(buf, sizeof buf, "lit %.3f, +100 ms %.3f, +1.1 s %.3f", lit, after100, after1s);
        check(lit > 0.4 && lit < 1.0 && after100 < lit && after100 > 0.25 && after1s < 0.05,
              "lamp releases smoothly", buf);
    }
}

} // namespace

//------------------------------------------------------------------------
int main()
{
    std::printf("hifitest - WetHiFi engine\n");
    testNull();
    testResponse();
    testExciter();
    testStability();
    testInPlace();
    testClicks();
    testRescale();
    testLamps();
    std::printf("\n%d passed, %d failed\n", passes, failures);
    return failures ? 1 : 0;
}
