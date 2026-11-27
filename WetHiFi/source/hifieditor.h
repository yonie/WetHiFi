//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------
// WetHiFi's editor: the line's WetEditor (weteditor.h, kept unchanged) plus
// one thing - it hands the edit controller to the lamp, which reads six
// parameters and so cannot be driven by a single control tag.
//------------------------------------------------------------------------

#pragma once

#include "weteditor.h"
#include "lampview.h"

namespace Yonie {

class HiFiEditor : public WetEditor
{
public:
    using WetEditor::WetEditor;

    VSTGUI::CView* verifyView(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
                              const VSTGUI::IUIDescription* description) override
    {
        if (auto* lamp = dynamic_cast<LampView*>(view))
            lamp->setController(getController());
        return WetEditor::verifyView(view, attributes, description);
    }
};

} // namespace Yonie
