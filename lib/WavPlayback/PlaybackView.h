#pragma once

#include "thefly_common.h"

#include <stddef.h>
#include <stdint.h>
#include <memory>

#include "../FlyGui/FlyGui.h"
#include "FilePlayback.h"
#include "ScrubBar.h"

class PlaybackView : public FlyGuiView
{
public:
    PlaybackView();

    void configureFile(const char* path);
    void pumpPlayback();
    bool playbackActive() const;
    void stopPlayback();

    void onLoad() override;
    void onUnload() override;
    bool handleTouch(const FlyGuiTouchEvent& event) override;
    bool redraw(bool forced) override;

    void onPressLeft() override;
    void onPressMid() override;
    void onPressRight() override;

private:
    static void playPauseThunk(uint32_t pressDurationMs);
    static void volumeThunk(uint32_t pressDurationMs);
    static void exitThunk(uint32_t pressDurationMs);
    static void deleteThunk(uint32_t pressDurationMs);
    static void scrubThunk(uint32_t positionMs, void* context);
    static void precisionScrubThunk(uint32_t positionMs, void* context);

    void handlePlayPause();
    void handleVolume();
    void handleExit();
    void handleDelete();
    void handleScrub(uint32_t positionMs);
    void handlePrecisionScrub(uint32_t positionMs);

    void startPlayback();
    void layoutItems();
    void syncControls();
    void syncPrecisionScrubBar(uint32_t currentMs, uint32_t totalMs, bool playing);
    void syncDeleteButton(bool playing);
    void setVolumeIndex(uint8_t index);
    void setDeleteButtonVisible(bool visible);
    void setDeleteArmed(bool armed);
    void showDeleteResultDialog(bool deleted, const char* detail);
    void drawFrame(bool forced);
    void drawFileName() const;
    void drawDeleteConfirmText() const;

    static PlaybackView* activeInstance_;

    FlyGuiItem recordIcon_;
    FlyGuiItem deleteButton_;
    FlyGuiItem playPauseButton_;
    FlyGuiItem volumeButton_;
    FlyGuiItem exitButton_;
    ScrubBar   scrubBar_;
    ScrubBar   precisionScrubBar_;

    char                          path_[96]                   = {};
    char                          fileName_[64]               = {};
    char                          statusText_[96]             = {};
    uint8_t                       volumeIndex_                = 0;
    bool                          playIconPause_              = false;
    bool                          deleteArmed_                = false;
    bool                          frameDirty_                 = true;
    bool                          precisionWindowInitialized_ = false;
    uint32_t                      deleteArmedAtMs_            = 0;
    std::unique_ptr<FilePlayback> playback_;
};
