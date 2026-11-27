//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/controls/ccontrol.h"

namespace Yonie {

//------------------------------------------------------------------------
// PeakLed - one LED that is the whole output meter (Ronald, 2026-11-20).
//
// The line's meter rules (meterlevel.h) for a meter of one lamp: dark below
// -48 dBFS, green from there, yellow from -12 dBFS, red above 0 dBFS. The
// panel art carries the lens switched off; this view paints the light on it.
//------------------------------------------------------------------------
class PeakLed : public VSTGUI::CControl
{
public:
    explicit PeakLed(const VSTGUI::CRect& size);

    // the lens, in view coordinates
    void setLens(const VSTGUI::CPoint& centre, VSTGUI::CCoord radius);

    void draw(VSTGUI::CDrawContext* context) override;
    void setValue(float val) override;

    CLASS_METHODS(PeakLed, VSTGUI::CControl)

private:
    VSTGUI::CPoint lensCentre;
    VSTGUI::CCoord lensRadius = 0;
    int zone = -1;              // -1 dark, 0 green, 1 yellow, 2 red
};

//------------------------------------------------------------------------
} // namespace Yonie
