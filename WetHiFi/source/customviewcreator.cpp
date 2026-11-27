//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#include "customviewcreator.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/lib/cframe.h"

#include <cstdio>

using namespace VSTGUI;

namespace Yonie {

static const std::string kAttrStepCount   = "step-count";
static const std::string kAttrCoarseStep  = "coarse-step";
static const std::string kAttrGlassCentre = "glass-centre";
static const std::string kAttrGlassRadius = "glass-radius";

//------------------------------------------------------------------------
// HiFiKnobCreator
//------------------------------------------------------------------------
HiFiKnobCreator::HiFiKnobCreator()
{
    UIViewFactory::registerViewCreator(*this);
}

CView* HiFiKnobCreator::create(const UIAttributes&, const IUIDescription*) const
{
    return new HiFiKnob(CRect(0, 0, 60, 60));
}

// UIViewFactory walks getBaseViewName() and runs the CAnimKnob and CControl
// creators for us, which is where the bitmap and the control tag come from.
bool HiFiKnobCreator::apply(CView* view, const UIAttributes& attributes,
                            const IUIDescription*) const
{
    auto* knob = dynamic_cast<HiFiKnob*>(view);
    if (!knob)
        return false;

    int32_t steps = 0;
    if (attributes.getIntegerAttribute(kAttrStepCount, steps) && steps > 1)
        knob->setStepCount(steps);

    int32_t coarse = 0;
    if (attributes.getIntegerAttribute(kAttrCoarseStep, coarse) && coarse > 1)
        knob->setCoarseStep(coarse);

    return true;
}

bool HiFiKnobCreator::getAttributeNames(StringList& names) const
{
    names.emplace_back(kAttrStepCount);
    names.emplace_back(kAttrCoarseStep);
    return true;
}

IViewCreator::AttrType HiFiKnobCreator::getAttributeType(const string& name) const
{
    if (name == kAttrStepCount)  return kIntegerType;
    if (name == kAttrCoarseStep) return kIntegerType;
    return kUnknownType;
}

bool HiFiKnobCreator::getAttributeValue(CView* view, const string& name,
                                        string& value, const IUIDescription*) const
{
    auto* knob = dynamic_cast<HiFiKnob*>(view);
    if (!knob)
        return false;
    if (name == kAttrStepCount)  { value = std::to_string(knob->getStepCount());  return true; }
    if (name == kAttrCoarseStep) { value = std::to_string(knob->getCoarseStep()); return true; }
    return false;
}

//------------------------------------------------------------------------
// LampViewCreator
//------------------------------------------------------------------------
LampViewCreator::LampViewCreator()
{
    UIViewFactory::registerViewCreator(*this);
}

CView* LampViewCreator::create(const UIAttributes&, const IUIDescription*) const
{
    return new LampView(CRect(0, 0, 200, 200));
}

bool LampViewCreator::apply(CView* view, const UIAttributes& attributes,
                            const IUIDescription*) const
{
    auto* lamp = dynamic_cast<LampView*>(view);
    if (!lamp)
        return false;
    CPoint centre;
    double radius = 0.0;
    const bool hasCentre = attributes.getPointAttribute(kAttrGlassCentre, centre);
    const bool hasRadius = attributes.getDoubleAttribute(kAttrGlassRadius, radius);
    if (hasCentre && hasRadius)
        lamp->setGlass(centre, radius);
    return true;
}

bool LampViewCreator::getAttributeNames(StringList& names) const
{
    names.emplace_back(kAttrGlassCentre);
    names.emplace_back(kAttrGlassRadius);
    return true;
}

IViewCreator::AttrType LampViewCreator::getAttributeType(const string& name) const
{
    if (name == kAttrGlassCentre) return kPointType;
    if (name == kAttrGlassRadius) return kFloatType;
    return kUnknownType;
}

//------------------------------------------------------------------------
// PeakLedCreator
//------------------------------------------------------------------------
static const std::string kAttrLensCentre = "lens-centre";
static const std::string kAttrLensRadius = "lens-radius";

PeakLedCreator::PeakLedCreator()
{
    UIViewFactory::registerViewCreator(*this);
}

CView* PeakLedCreator::create(const UIAttributes&, const IUIDescription*) const
{
    return new PeakLed(CRect(0, 0, 40, 40));
}

bool PeakLedCreator::apply(CView* view, const UIAttributes& attributes, const IUIDescription*) const
{
    auto* led = dynamic_cast<PeakLed*>(view);
    if (!led)
        return false;
    CPoint centre;
    double radius = 0.0;
    if (attributes.getPointAttribute(kAttrLensCentre, centre) &&
        attributes.getDoubleAttribute(kAttrLensRadius, radius))
        led->setLens(centre, radius);
    return true;
}

bool PeakLedCreator::getAttributeNames(StringList& names) const
{
    names.emplace_back(kAttrLensCentre);
    names.emplace_back(kAttrLensRadius);
    return true;
}

IViewCreator::AttrType PeakLedCreator::getAttributeType(const string& name) const
{
    if (name == kAttrLensCentre) return kPointType;
    if (name == kAttrLensRadius) return kFloatType;
    return kUnknownType;
}

//------------------------------------------------------------------------
void registerCustomViews()
{
    // Static so each creator registers exactly once, however many editor
    // windows the host opens.
    static HiFiKnobCreator knobCreator;
    static LampViewCreator lampCreator;
    static PeakLedCreator peakLedCreator;
    VSTGUI::CFrame::kDefaultKnobMode = VSTGUI::kLinearMode;
}

//------------------------------------------------------------------------
} // namespace Yonie
