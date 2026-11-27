//------------------------------------------------------------------------
// Copyright(c) 2026 Yonie.
//------------------------------------------------------------------------

#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cbitmap.h"
#include "vstgui/lib/events.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

#include <complex>
#include <cstdint>
#include <vector>

namespace Yonie {

class WetHiFiController;

//------------------------------------------------------------------------
// LampView - the round screen (it began as a lamp, hence the name).
//
// Three screens, a click on the glass moves to the next (Ronald, 2026-11-20):
//   0  oscilloscope: the output's waveform, triggered so it stands still
//   1  spectrogram: frequency against time, scrolling right to left
//   2  spectrum analyser: the output's spectrum as a glowing curve
// The colour is the knobs' mix and nothing else; the output's VU level lifts
// the glow behind the picture. Everything is drawn live at 60 frames a second
// from the full-rate samples the processor sends (hifiscope.h).
//------------------------------------------------------------------------
class LampView : public VSTGUI::CView
{
public:
    explicit LampView(const VSTGUI::CRect& size);
    ~LampView() override;

    void setController(Steinberg::Vst::EditController* c);

    // The glass, in view coordinates: centre and radius of the screen. The
    // view itself is larger so the bloom can spill onto the bezel.
    void setGlass(const VSTGUI::CPoint& centre, VSTGUI::CCoord radius);

    void draw(VSTGUI::CDrawContext* context) override;
    bool attached(VSTGUI::CView* parent) override;
    bool removed(VSTGUI::CView* parent) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;

    static constexpr int kTrail = 4;    // oscilloscope traces kept for the afterglow
    static constexpr int kFFT = 8192;   // 6 Hz bins at 48 kHz, so the low end resolves
    static constexpr int kBands = 128;  // log-spaced bands, 20 Hz to 20 kHz

    CLASS_METHODS(LampView, VSTGUI::CView)

private:
    void tick();
    void analyse();                     // FFT of the newest samples into `bands`
    void pushSpectrogramColumn();
    void drawScope(VSTGUI::CDrawContext* context, VSTGUI::CPoint c, VSTGUI::CCoord r);
    void drawSpectrogram(VSTGUI::CDrawContext* context, VSTGUI::CPoint c, VSTGUI::CCoord r);
    void drawAnalyser(VSTGUI::CDrawContext* context, VSTGUI::CPoint c, VSTGUI::CCoord r);

    Steinberg::Vst::EditController* controller = nullptr;
    WetHiFiController* hifi = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> timer;

    VSTGUI::CPoint glassCentre;
    VSTGUI::CCoord glassRadius = 0;
    bool glassSet = false;

    double knob[3] = {0, 0, 0};         // the colour
    double vu = 0;                      // the glow
    double mixR = 0.86, mixG = 0.92, mixB = 0.88;

    // oscilloscope
    std::vector<float> trail[kTrail];
    uint64_t lastSerial = 0;

    // spectrum
    std::vector<float> window;          // Hann
    std::vector<std::complex<float>> fft;
    float bands[kBands] = {};           // 0..1, smoothed
    std::vector<float> columns;         // spectrogram history: width x kBands, ring
    int colWidth = 0;
    int colHead = 0;
    VSTGUI::SharedPointer<VSTGUI::CBitmap> sgram;

    int shownMode = -1;                 // to restart the mode label on a change
};

//------------------------------------------------------------------------
} // namespace Yonie
