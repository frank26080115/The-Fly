#pragma once

#include "../FlyGui/FlyGui.h"
#include "AudioManager.h"

class MasterTestView : public FlyGuiView
{
public:
    MasterTestView();

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    bool redraw(bool forced) override;

private:
    enum class OutputMode
    {
        None,
        Earbud,
        InternalSpeaker,
    };

    enum class MicMode
    {
        None,
        EarbudMic,
        InternalLineInMic,
    };

    void toggleOutput(OutputMode mode);
    void toggleMic(MicMode mode);
    bool applyAudioRouting();
    void resetAudioRouting();
    void queueSineAudio();
    void resetMicHistory();
    void updateMicHistory();
    void drawFrame();
    void drawStatusCell();
    void drawMicHistory();
    void drawButtons();

    OutputMode outputMode_       = OutputMode::None;
    MicMode    micMode_          = MicMode::None;
    uint32_t   lastRefreshMs_    = 0;
    uint32_t   lastExtCodecGeneration_ = 0;
    uint32_t   sinePhase_        = 0;
    bool       routeOk_          = true;
    uint8_t    micHistory_[100]  = {};
};
