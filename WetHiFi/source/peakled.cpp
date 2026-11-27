//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "peakled.h"
#include "meterlevel.h"

#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>

using namespace VSTGUI;

namespace Yonie {

namespace {

// The zone a reading falls in, by the line's meter rules.
int zoneOf(float normalized)
{
    if (normalized <= 1e-6f)
        return -1;
    const double db = Meter::decodeDb(normalized);
    if (db > Meter::kHighDb)
        return 2;                       // over 0 dBFS
    if (db >= -12.0)
        return 1;                       // the yellow zone
    if (db >= Meter::kLowDb)
        return 0;                       // green
    return -1;                          // too quiet to light
}

// The colours the line's meters light in.
const CColor kLit[3] = {CColor(70, 230, 90), CColor(250, 210, 50), CColor(255, 60, 50)};

} // namespace

//------------------------------------------------------------------------
PeakLed::PeakLed(const CRect& size)
: CControl(size)
{
    setTransparency(true);
    setMouseEnabled(false);
}

void PeakLed::setLens(const CPoint& centre, CCoord radius)
{
    lensCentre = centre;
    lensRadius = radius;
}

//------------------------------------------------------------------------
void PeakLed::setValue(float val)
{
    CControl::setValue(val);
    const int z = zoneOf(val);
    if (z != zone)
    {
        zone = z;
        invalid();
    }
}

//------------------------------------------------------------------------
void PeakLed::draw(CDrawContext* context)
{
    setDirty(false);

    // The panel renders the LED in all four states (off, green, yellow, red), cut
    // out of the renders like every LED in the line, so it sits in the same light
    // and softness as the rest of the panel (Ronald, 2026-11-20).
    if (auto* frames = dynamic_cast<CMultiFrameBitmap*>(getDrawBackground()))
    {
        frames->drawFrame(context, static_cast<uint16_t>(zone + 1), getViewSize().getTopLeft());
        return;
    }

    // Fallback without the strip: draw it.
    if (zone < 0)
        return;                         // dark: the lens in the backplate is the picture

    const CRect vs = getViewSize();
    const CPoint c = lensRadius > 0 ? vs.getTopLeft() + lensCentre : vs.getCenter();
    const CCoord r = lensRadius > 0 ? lensRadius : std::min(vs.getWidth(), vs.getHeight()) * 0.2;
    const CColor k = kLit[zone];
    auto withAlpha = [](CColor col, uint8_t a) { col.alpha = a; return col; };
    auto white = [&](double t) {
        return CColor(uint8_t(k.red + (255 - k.red) * t), uint8_t(k.green + (255 - k.green) * t),
                      uint8_t(k.blue + (255 - k.blue) * t), 255);
    };

    context->setDrawMode(kAntiAliasing | kNonIntegralMode);
    auto fill = [&](CCoord rad, std::initializer_list<std::pair<double, CColor>> stops) {
        auto p = owned(context->createGraphicsPath());
        auto g = owned(CGradient::create(GradientColorStopMap()));
        if (!p || !g)
            return;
        p->addEllipse(CRect(c.x - rad, c.y - rad, c.x + rad, c.y + rad));
        for (const auto& s : stops)
            g->addColorStop(s.first, s.second);
        context->fillRadialGradient(p, *g, c, rad);
    };
    // the glow on the panel round the lens
    fill(r * 3.2, {{0.0, withAlpha(k, 150)}, {0.30, withAlpha(k, 70)}, {1.0, withAlpha(k, 0)}});
    // the lit lens, hottest in the middle
    fill(r, {{0.0, white(0.75)}, {0.55, white(0.15)}, {1.0, withAlpha(k, 230)}});
}

//------------------------------------------------------------------------
} // namespace Yonie
