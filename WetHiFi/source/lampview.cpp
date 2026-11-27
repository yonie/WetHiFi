//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "lampview.h"
#include "wethificids.h"
#include "wethificontroller.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cstring.h"

#include <algorithm>
#include <cmath>

using namespace VSTGUI;

namespace Yonie {

namespace {

// The three primaries: the panel's section colours raised to full brightness,
// so the picture can only ever be a colour the panel already owns.
// Order: highs, mids, bass - the order of the knobs.
struct RGBf { double r, g, b; };
const RGBf kPrimary[3] = {
    {0.30, 0.71, 1.00},   // SILKY HIGHS  - blue
    {0.61, 1.00, 0.58},   // FOCUSED MIDS - green
    {1.00, 0.60, 0.32},   // TIGHT BASS   - tan
};
// With every knob at 0 the picture is a plain phosphor white.
const RGBf kNeutral = {0.86, 0.92, 0.88};

const Steinberg::Vst::ParamID kKnobIds[3] = {kHighsParam, kMidsParam, kBassParam};

constexpr uint32_t kTickMs = 16;        // 60 frames a second
constexpr int kGridX = 80;              // spectrogram cells across ...
constexpr int kGridY = 56;              // ... and up

inline uint8_t to8(double v)
{
    return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
}

// In-place radix-2 FFT.
void fftInPlace(std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const float ang = -2.0f * 3.14159265358979f / float(len);
        const std::complex<float> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

} // namespace

//------------------------------------------------------------------------
LampView::LampView(const CRect& size)
: CView(size)
{
    setTransparency(true);
    setMouseEnabled(true);
    window.resize(kFFT);
    for (int i = 0; i < kFFT; ++i)
        window[i] = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979f * float(i) / float(kFFT - 1));
    fft.resize(kFFT);
    colWidth = kGridX;
    columns.assign(size_t(kGridX) * kBands, 0.0f);
}

LampView::~LampView() = default;

//------------------------------------------------------------------------
void LampView::setController(Steinberg::Vst::EditController* c)
{
    controller = c;
    hifi = dynamic_cast<WetHiFiController*>(c);
}

//------------------------------------------------------------------------
void LampView::setGlass(const CPoint& centre, CCoord radius)
{
    glassCentre = centre;
    glassRadius = radius;
    glassSet = radius > 0;
}

//------------------------------------------------------------------------
bool LampView::attached(CView* parent)
{
    if (!CView::attached(parent))
        return false;
    timer = makeOwned<CVSTGUITimer>([this](CVSTGUITimer*) { tick(); }, kTickMs, true);
    return true;
}

bool LampView::removed(CView* parent)
{
    if (timer)
    {
        timer->stop();
        timer = nullptr;
    }
    return CView::removed(parent);
}

//------------------------------------------------------------------------
// A click anywhere on the glass moves to the next screen.
void LampView::onMouseDownEvent(MouseDownEvent& event)
{
    if (!hifi || !event.buttonState.isLeft())
        return;
    const CRect vs = getViewSize();
    const CPoint c = glassSet ? vs.getTopLeft() + glassCentre : vs.getCenter();
    const CCoord r = glassSet ? glassRadius : std::min(vs.getWidth(), vs.getHeight()) * 0.35;
    const CPoint d = event.mousePosition - c;
    if (d.x * d.x + d.y * d.y > r * r)
        return;
    hifi->setViewMode(hifi->viewMode() + 1);
    invalid();
    event.consumed = true;
}

//------------------------------------------------------------------------
void LampView::tick()
{
    if (!controller)
        return;
    bool changed = false;

    // the colour: the knobs, straight away
    double k[3];
    for (int b = 0; b < 3; ++b)
    {
        k[b] = controller->getParamNormalized(kKnobIds[b]);
        if (k[b] != knob[b])
        {
            knob[b] = k[b];
            changed = true;
        }
    }
    if (changed)
    {
        if (knob[0] + knob[1] + knob[2] <= 1e-4)
        {
            mixR = kNeutral.r;
            mixG = kNeutral.g;
            mixB = kNeutral.b;
        }
        else
        {
            double r = 0, g = 0, bl = 0;
            for (int b = 0; b < 3; ++b)
            {
                r += kPrimary[b].r * knob[b];
                g += kPrimary[b].g * knob[b];
                bl += kPrimary[b].b * knob[b];
            }
            const double peak = std::max({r, g, bl});
            mixR = r / peak;
            mixG = g / peak;
            mixB = bl / peak;
        }
    }

    const double lv = std::clamp(controller->getParamNormalized(kLampLevel), 0.0, 1.0);
    if (std::abs(lv - vu) > 1e-4)
    {
        vu = lv;
        changed = true;
    }

    if (hifi && hifi->scopeSerial() != lastSerial)
    {
        lastSerial = hifi->scopeSerial();
        const auto& s = hifi->scope();
        const int n = static_cast<int>(hifi->scopeCount());
        if (n >= 1536)
        {
            // oscilloscope: trigger on a rising zero crossing in the first
            // quarter, then 1536 samples decimated by three
            int start = 0;
            for (int i = 1; i < n / 4; ++i)
                if (s[i - 1] < 0.0f && s[i] >= 0.0f)
                {
                    start = i;
                    break;
                }
            for (int t = 0; t < kTrail - 1; ++t)
                trail[t] = trail[t + 1];
            auto& tr = trail[kTrail - 1];
            tr.clear();
            for (int i = start; i < start + 1536 && i < n; i += 3)
                tr.push_back(s[i]);
            // spectrum: always kept running, so a switch shows history at once
            analyse();
            pushSpectrogramColumn();
        }
        changed = true;
    }

    if (hifi && hifi->viewMode() != shownMode)
    {
        shownMode = hifi->viewMode();
        changed = true;
    }
    if (changed)
        invalid();
}

//------------------------------------------------------------------------
void LampView::analyse()
{
    // the longer history the controller keeps, so the bass resolves
    static thread_local std::vector<float> s(kFFT);
    hifi->copyHistory(s.data(), kFFT);
    const float sr = std::max(1000.0f, hifi->scopeRate());
    double wsum = 0;
    for (int i = 0; i < kFFT; ++i)
    {
        fft[i] = std::complex<float>(s[i] * window[i], 0.0f);
        wsum += window[i];
    }
    fftInPlace(fft);
    const double norm = 2.0 / wsum;     // a full-scale sine reads 0 dBFS
    for (int b = 0; b < kBands; ++b)
    {
        const double flo = 20.0 * std::pow(1000.0, double(b) / kBands);
        const double fhi = 20.0 * std::pow(1000.0, double(b + 1) / kBands);
        const double kf = std::sqrt(flo * fhi) / sr * kFFT;     // the band's centre, in bins
        int klo = int(std::ceil(flo / sr * kFFT));
        int khi = int(std::floor(fhi / sr * kFFT));
        double p = 0;
        if (khi - klo >= 1)
        {
            // a band wider than a bin: its loudest bin
            klo = std::clamp(klo, 1, kFFT / 2 - 1);
            khi = std::clamp(khi, klo, kFFT / 2 - 1);
            for (int k = klo; k <= khi; ++k)
                p = std::max(p, double(std::norm(fft[k])));
        }
        else
        {
            // narrower than a bin (the low end): interpolate between the two
            // bins either side of its centre, so neighbouring bands differ
            const int k0 = std::clamp(int(kf), 1, kFFT / 2 - 2);
            const double t = std::clamp(kf - k0, 0.0, 1.0);
            p = std::norm(fft[k0]) * (1.0 - t) + std::norm(fft[k0 + 1]) * t;
        }
        double db = 10.0 * std::log10(p * norm * norm + 1e-14);
        // a gentle upward tilt from 1 kHz, so programme reads level across the band
        db += 3.0 * std::log2(std::sqrt(flo * fhi) / 1000.0);
        const float v = float(std::clamp((db + 84.0) / 78.0, 0.0, 1.0));
        bands[b] = v > bands[b] ? v : bands[b] * 0.86f + v * 0.14f;
    }
}

void LampView::pushSpectrogramColumn()
{
    for (int b = 0; b < kBands; ++b)
        columns[size_t(colHead) * kBands + b] = bands[b];
    colHead = (colHead + 1) % colWidth;
}

//------------------------------------------------------------------------
void LampView::draw(CDrawContext* context)
{
    setDirty(false);

    const CRect vs = getViewSize();
    const CPoint c = glassSet ? vs.getTopLeft() + glassCentre : vs.getCenter();
    const CCoord r = glassSet ? glassRadius : std::min(vs.getWidth(), vs.getHeight()) * 0.35;

    auto col = [&](double k, double whiten, double alpha) {
        return CColor(to8(mixR * k + (1 - mixR * k) * whiten), to8(mixG * k + (1 - mixG * k) * whiten),
                      to8(mixB * k + (1 - mixB * k) * whiten), to8(alpha));
    };
    auto radial = [&](CCoord rad, std::initializer_list<std::pair<double, CColor>> stops) {
        auto p = owned(context->createGraphicsPath());
        auto g = owned(CGradient::create(GradientColorStopMap()));
        if (!p || !g)
            return;
        p->addEllipse(CRect(c.x - rad, c.y - rad, c.x + rad, c.y + rad));
        for (const auto& s : stops)
            g->addColorStop(s.first, s.second);
        context->fillRadialGradient(p, *g, c, rad);
    };

    context->setDrawMode(kAntiAliasing | kNonIntegralMode);

    // the screen: plain dark glass, no glow, no haze over the picture
    radial(r, {{0.00, CColor(12, 13, 13, 255)}, {1.00, CColor(6, 7, 7, 255)}});
    (void)col;

    const int mode = hifi ? hifi->viewMode() : 0;
    if (mode == 1)
        drawSpectrogram(context, c, r);
    else if (mode == 2)
        drawAnalyser(context, c, r);
    else
        drawScope(context, c, r);
}

//------------------------------------------------------------------------
// The oscilloscope: the newest trace and five older ones, nothing else.
void LampView::drawScope(CDrawContext* context, CPoint c, CCoord r)
{
    auto col = [&](double whiten, double alpha) {
        return CColor(to8(mixR + (1 - mixR) * whiten), to8(mixG + (1 - mixG) * whiten),
                      to8(mixB + (1 - mixB) * whiten), to8(alpha));
    };
    // A plain scope, redrawn every frame: the newest trace as one sharp line,
    // the five before it as the same thin line, fainter the older it is (Ronald,
    // 2026-11-27). Linear amplitude; the line is only clipped where it would
    // leave the glass, never scaled down towards the rim.
    static constexpr double kShadow[kTrail] = {0.06, 0.10, 0.15, 0.22, 0.32, 1.00};
    context->setLineStyle(CLineStyle(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound));
    context->setLineWidth(std::max(1.5, r * 0.014));
    for (int k = 0; k < kTrail; ++k)
    {
        const auto& s = trail[k];
        if (s.size() < 2)
            continue;
        auto p = owned(context->createGraphicsPath());
        if (!p)
            continue;
        const double span = 0.92;
        const size_t n = s.size();
        for (size_t i = 0; i < n; ++i)
        {
            const double u = -span + 2.0 * span * double(i) / double(n - 1);
            const double lim = 0.96 * std::sqrt(std::max(0.0, 1.0 - u * u));
            const double y = std::clamp(double(s[i]) * 0.80, -lim, lim);
            const CPoint pt(c.x + u * r, c.y - y * r);
            if (i == 0)
                p->beginSubpath(pt);
            else
                p->addLine(pt);
        }
        context->setFrameColor(col(0.55, kShadow[k]));
        context->drawGraphicsPath(p, CDrawContext::kPathStroked);
    }
}

//------------------------------------------------------------------------
// The spectrogram: frequency up the screen (log, 20 Hz to 20 kHz), time across
// it, newest on the right, each cell lit in the colour by its level. Cells
// outside the round glass are skipped.
void LampView::drawSpectrogram(CDrawContext* context, CPoint c, CCoord r)
{
    const CCoord side = r * 1.84;
    const CCoord cw = side / kGridX, ch = side / kGridY;
    const CCoord x0 = c.x - side / 2, y0 = c.y - side / 2;
    const CCoord lim = r * 0.94;
    for (int gx = 0; gx < kGridX; ++gx)
    {
        // the column of history this cell row shows: oldest at the left
        const int colIdx = (colHead + int(double(gx) * colWidth / kGridX)) % colWidth;
        const float* colv = &columns[size_t(colIdx) * kBands];
        const CCoord cx = x0 + (gx + 0.5) * cw;
        for (int gy = 0; gy < kGridY; ++gy)
        {
            const CCoord cy = y0 + (gy + 0.5) * ch;
            const CCoord dx = cx - c.x, dy = cy - c.y;
            if (dx * dx + dy * dy > lim * lim)
                continue;
            const double pos = double(kGridY - 1 - gy) / (kGridY - 1) * (kBands - 1);
            const int b0 = int(pos);
            const int b1 = std::min(kBands - 1, b0 + 1);
            const double v = colv[b0] + (colv[b1] - colv[b0]) * (pos - b0);
            const double t = std::pow(std::clamp(v, 0.0, 1.0), 1.6);
            if (t < 0.02)
                continue;
            const double w = std::max(0.0, t - 0.7) * 2.0;
            context->setFillColor(CColor(to8((mixR + (1 - mixR) * w) * t * 1.15),
                                         to8((mixG + (1 - mixG) * w) * t * 1.15),
                                         to8((mixB + (1 - mixB) * w) * t * 1.15),
                                         to8(std::min(1.0, t * 1.4))));
            context->drawRect(CRect(cx - cw / 2, cy - ch / 2, cx + cw / 2 + 0.6, cy + ch / 2 + 0.6),
                              kDrawFilled);
        }
    }
}

//------------------------------------------------------------------------
// The spectrum analyser: the bands as one glowing curve across the screen
// (log frequency, left to right), the area under it filled, both kept inside
// the round glass.
void LampView::drawAnalyser(CDrawContext* context, CPoint c, CCoord r)
{
    auto col = [&](double whiten, double alpha) {
        return CColor(to8(mixR + (1 - mixR) * whiten), to8(mixG + (1 - mixG) * whiten),
                      to8(mixB + (1 - mixB) * whiten), to8(alpha));
    };
    const double span = 0.90;
    auto pointAt = [&](int b, double level) {
        const double u = -span + 2.0 * span * double(b) / double(kBands - 1);
        const double half = std::sqrt(std::max(0.0, 1.0 - u * u)) * 0.90;
        const double y = half - level * 2.0 * half;     // +half at the bottom, -half at the top
        return CPoint(c.x + u * r, c.y + y * r);
    };
    // a faint frequency grid: 100 Hz, 1 kHz, 10 kHz
    if (auto p = owned(context->createGraphicsPath()))
    {
        for (double f : {100.0, 1000.0, 10000.0})
        {
            const double b = std::log(f / 20.0) / std::log(1000.0) * kBands;
            const CPoint top = pointAt(int(b), 1.0), bot = pointAt(int(b), 0.0);
            p->beginSubpath(top);
            p->addLine(bot);
        }
        context->setLineStyle(kLineSolid);
        context->setLineWidth(std::max(1.0, r * 0.006));
        context->setFrameColor(col(0.0, 0.12));
        context->drawGraphicsPath(p, CDrawContext::kPathStroked);
    }
    auto curve = owned(context->createGraphicsPath());
    auto area = owned(context->createGraphicsPath());
    if (!curve || !area)
        return;
    for (int b = 0; b < kBands; ++b)
    {
        const CPoint pt = pointAt(b, bands[b]);
        if (b == 0)
        {
            curve->beginSubpath(pt);
            area->beginSubpath(pointAt(0, 0.0));
        }
        else
            curve->addLine(pt);
        area->addLine(pt);
    }
    for (int b = kBands - 1; b >= 0; --b)
        area->addLine(pointAt(b, 0.0));
    area->closeSubpath();
    if (auto g = owned(CGradient::create(GradientColorStopMap())))
    {
        g->addColorStop(0.0, col(0.0, 0.45));
        g->addColorStop(1.0, col(0.0, 0.04));
        context->fillLinearGradient(area, *g, CPoint(c.x, c.y - r * 0.9), CPoint(c.x, c.y + r * 0.9));
    }
    const CLineStyle round(CLineStyle::kLineCapRound, CLineStyle::kLineJoinRound);
    context->setLineStyle(round);
    context->setLineWidth(r * 0.06);
    context->setFrameColor(col(0.0, 0.12));
    context->drawGraphicsPath(curve, CDrawContext::kPathStroked);
    context->setLineWidth(r * 0.026);
    context->setFrameColor(col(0.0, 0.35));
    context->drawGraphicsPath(curve, CDrawContext::kPathStroked);
    context->setLineWidth(std::max(1.2, r * 0.011));
    context->setFrameColor(col(0.55, 0.95));
    context->drawGraphicsPath(curve, CDrawContext::kPathStroked);
}

//------------------------------------------------------------------------
} // namespace Yonie
