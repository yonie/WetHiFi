//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/iuidescription.h"

#include "hifiknob.h"
#include "lampview.h"
#include "peakled.h"

namespace Yonie {

//------------------------------------------------------------------------
// HiFiKnobCreator - the stepped filmstrip knob. Two attributes: how many
// positions the strip holds, and how many of them make one ordinary detent.
//------------------------------------------------------------------------
class HiFiKnobCreator : public VSTGUI::ViewCreatorAdapter
{
public:
    HiFiKnobCreator();

    VSTGUI::IdStringPtr getViewName() const override { return "HiFiKnob"; }
    VSTGUI::IdStringPtr getBaseViewName() const override { return "CAnimKnob"; }
    VSTGUI::UTF8StringPtr getDisplayName() const override { return "HiFi Knob"; }

    VSTGUI::CView* create(const VSTGUI::UIAttributes& attributes,
                          const VSTGUI::IUIDescription* description) const override;
    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override;
    bool getAttributeNames(StringList& attributeNames) const override;
    AttrType getAttributeType(const string& attributeName) const override;
    bool getAttributeValue(VSTGUI::CView* view, const string& attributeName,
                           string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override;
};

//------------------------------------------------------------------------
// LampViewCreator - the live light behind the glass. Two attributes: the
// glass centre ("x, y" in view coordinates) and its radius.
//------------------------------------------------------------------------
class LampViewCreator : public VSTGUI::ViewCreatorAdapter
{
public:
    LampViewCreator();

    VSTGUI::IdStringPtr getViewName() const override { return "LampView"; }
    VSTGUI::IdStringPtr getBaseViewName() const override { return "CView"; }
    VSTGUI::UTF8StringPtr getDisplayName() const override { return "Lamp View"; }

    VSTGUI::CView* create(const VSTGUI::UIAttributes& attributes,
                          const VSTGUI::IUIDescription* description) const override;
    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override;
    bool getAttributeNames(StringList& attributeNames) const override;
    AttrType getAttributeType(const string& attributeName) const override;
};

//------------------------------------------------------------------------
// PeakLedCreator - the single output LED. Attributes: the lens centre ("x, y"
// in view coordinates) and its radius.
//------------------------------------------------------------------------
class PeakLedCreator : public VSTGUI::ViewCreatorAdapter
{
public:
    PeakLedCreator();

    VSTGUI::IdStringPtr getViewName() const override { return "PeakLed"; }
    VSTGUI::IdStringPtr getBaseViewName() const override { return "CControl"; }
    VSTGUI::UTF8StringPtr getDisplayName() const override { return "Peak LED"; }

    VSTGUI::CView* create(const VSTGUI::UIAttributes& attributes,
                          const VSTGUI::IUIDescription* description) const override;
    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override;
    bool getAttributeNames(StringList& attributeNames) const override;
    AttrType getAttributeType(const string& attributeName) const override;
};

//------------------------------------------------------------------------
void registerCustomViews();

//------------------------------------------------------------------------
} // namespace Yonie
