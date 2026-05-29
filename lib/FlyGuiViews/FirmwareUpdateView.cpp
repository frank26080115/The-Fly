#include "FirmwareUpdateView.h"

#include <Arduino.h>
#include <stdio.h>

#include "Display.h"
#include "FlyGuiText.h"
#include "MicroSdCard.h"
#include "SpriteDraw.h"
#include "esp_log.h"
#include "esp_system.h"
#include "sprites.h"
#include "utilfuncs.h"

namespace
{
constexpr const char* TAG = "FirmwareUpdateView";
constexpr const char* kFirmwareUpdatePath = "/firmware.bin";
constexpr int16_t kContentY = FlyGui::kTopBarHeight;
constexpr int16_t kTopSpriteY = FlyGui::kTopBarHeight + 8;
constexpr int16_t kTextX = 18;
constexpr int16_t kTextY = FlyGui::kTopBarHeight + 118;
constexpr int16_t kTextWidth = 284;
constexpr int16_t kTextLineHeight = 17;
constexpr int16_t kButtonSize = 50;
constexpr int16_t kScreenWidth = 320;
constexpr int16_t kButtonColumnWidth = kScreenWidth / 3;
constexpr int16_t kGoX = (kButtonColumnWidth - kButtonSize) / 2;
constexpr int16_t kCancelX = kScreenWidth - kButtonColumnWidth + (kButtonColumnWidth - kButtonSize) / 2;
constexpr int16_t kButtonY = 185;
constexpr int16_t kHourglassSize = 60;
constexpr int16_t kHourglassY = 130;
constexpr int16_t kProgressWidth = 300;
constexpr int16_t kProgressHeight = 12;
constexpr int16_t kProgressY = 199;
constexpr int16_t kProgressTextY = kProgressY + kProgressHeight + 4;
constexpr int16_t kProgressTextHeight = 18;
constexpr uint8_t kTextFont = 2;
constexpr uint8_t kSmallTextFont = 1;
constexpr float kTextSize = 1.0f;

int16_t centered_x(uint32_t width)
{
    return static_cast<int16_t>((thefly_display.width() - static_cast<int32_t>(width)) / 2);
}

int16_t progress_x()
{
    return static_cast<int16_t>((thefly_display.width() - kProgressWidth) / 2);
}
} // namespace

FirmwareUpdateView* FirmwareUpdateView::activeView_ = nullptr;

FirmwareUpdateView::FirmwareUpdateView()
    : FlyGuiView(FLYGUI_VIEW_FIRMWARE_UPDATE),
      goItem_(kGoX, kButtonY, kButtonSize, kButtonSize),
      cancelItem_(kCancelX, kButtonY, kButtonSize, kButtonSize),
      progressBar_(0, kProgressY, kProgressWidth, kProgressHeight)
{
    goItem_.setSprite(sprite_greencheckmark_50,
                      SPRITE_GREENCHECKMARK_50_WIDTH,
                      SPRITE_GREENCHECKMARK_50_HEIGHT,
                      SPRITE_GREENCHECKMARK_50_BYTES);
    goItem_.setCallback(onGoTriggered);
    addItem(goItem_);

    cancelItem_.setSprite(sprite_canceldoor_50,
                          SPRITE_CANCELDOOR_50_WIDTH,
                          SPRITE_CANCELDOOR_50_HEIGHT,
                          SPRITE_CANCELDOOR_50_BYTES);
    cancelItem_.setCallback(onCancelTriggered);
    addItem(cancelItem_);
}

void FirmwareUpdateView::configure(bool batteryFullish)
{
    state_ = State::Prompt;
    batteryFullish_ = batteryFullish;
    dismissed_ = false;
    bytesWritten_ = 0;
    bytesTotal_ = 0;
    hourglassFrame_ = 0;
    result_ = MicroSdCard::FirmwareUpdateResult::Ok;
    goItem_.setVisible(batteryFullish_);
    cancelItem_.setVisible(true);
    setDirty();
}

void FirmwareUpdateView::onLoad()
{
    activeView_ = this;
    goItem_.setVisible(state_ == State::Prompt && batteryFullish_);
    cancelItem_.setVisible(state_ == State::Prompt);
    progressBar_.onLoad();
    FlyGuiView::onLoad();
}

void FirmwareUpdateView::onUnload()
{
    if (activeView_ == this)
    {
        activeView_ = nullptr;
    }
    FlyGuiView::onUnload();
}

bool FirmwareUpdateView::handleTouch(const FlyGuiTouchEvent& event)
{
    if (state_ == State::Complete)
    {
        if (event.justPressed || event.justReleased)
        {
            delay(50);
            esp_restart();
        }
        return true;
    }

    if (state_ != State::Prompt)
    {
        return true;
    }

    if (batteryFullish_ && (goItem_.contains(event.x, event.y) || goItem_.isPressed()))
    {
        return goItem_.handleTouch(event);
    }

    if (cancelItem_.contains(event.x, event.y) || cancelItem_.isPressed())
    {
        return cancelItem_.handleTouch(event);
    }

    return true;
}

void FirmwareUpdateView::redraw(bool forced)
{
    if (!forced && !dirty())
    {
        return;
    }

    switch (state_)
    {
    case State::Prompt:
        drawPrompt();
        break;
    case State::Updating:
        drawUpdating();
        break;
    case State::Complete:
        drawComplete();
        break;
    }

    markClean();
}

void FirmwareUpdateView::onPressLeft()
{
    if (state_ == State::Prompt && batteryFullish_)
    {
        goItem_.trigger();
    }
}

void FirmwareUpdateView::onPressRight()
{
    if (state_ == State::Prompt)
    {
        cancelItem_.trigger();
    }
}

void FirmwareUpdateView::onGoTriggered(uint32_t)
{
    if (activeView_)
    {
        activeView_->startUpdate();
    }
}

void FirmwareUpdateView::onCancelTriggered(uint32_t)
{
    if (activeView_)
    {
        activeView_->cancel();
    }
}

void FirmwareUpdateView::onProgress(uint64_t bytesWritten, uint64_t bytesTotal)
{
    if (activeView_)
    {
        activeView_->updateProgress(bytesWritten, bytesTotal);
    }
}

void FirmwareUpdateView::cancel()
{
    dismissed_ = true;
}

void FirmwareUpdateView::startUpdate()
{
    if (state_ != State::Prompt || !batteryFullish_)
    {
        return;
    }

    state_ = State::Updating;
    goItem_.setVisible(false);
    cancelItem_.setVisible(false);
    drawUpdating();

    const MicroSdCard::FirmwareUpdateResult result = MicroSdCard::update_firmware(onProgress);
    finishUpdate(result);
}

void FirmwareUpdateView::updateProgress(uint64_t bytesWritten, uint64_t bytesTotal)
{
    bytesWritten_ = bytesWritten;
    bytesTotal_ = bytesTotal;
    hourglassFrame_ = static_cast<uint8_t>((hourglassFrame_ + 1) % 3);
    drawHourglassFrame(hourglassFrame_);
    drawProgress();
}

void FirmwareUpdateView::finishUpdate(MicroSdCard::FirmwareUpdateResult result)
{
    result_ = result;
    if (result_ == MicroSdCard::FirmwareUpdateResult::Ok)
    {
        if (!MicroSdCard::fs().remove(kFirmwareUpdatePath))
        {
            ESP_LOGW(TAG, "firmware update succeeded, but deleting firmware.bin failed");
        }
    }

    state_ = State::Complete;
    drawComplete();
}

void FirmwareUpdateView::drawPrompt()
{
    thefly_display.fillRect(0,
                            kContentY,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - kContentY),
                            TFT_BLACK);

    drawTopSprite(sprite_fwupdate, SPRITE_FWUPDATE_BYTES, SPRITE_FWUPDATE_WIDTH, SPRITE_FWUPDATE_HEIGHT);

    drawMessage(batteryFullish_
                    ? "Firmware file detected\nDo you want to update firmware?"
                    : "Firmware file detected\nTo perform a firmware update, battery needs to be fully charged",
                static_cast<int16_t>(kButtonY - 4));

    if (batteryFullish_)
    {
        goItem_.redraw(true);
    }
    cancelItem_.redraw(true);
}

void FirmwareUpdateView::drawUpdating()
{
    thefly_display.fillRect(0,
                            kContentY,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - kContentY),
                            TFT_BLACK);

    drawTopSprite(sprite_fwupdate, SPRITE_FWUPDATE_BYTES, SPRITE_FWUPDATE_WIDTH, SPRITE_FWUPDATE_HEIGHT);
    hourglassFrame_ = 0;
    drawHourglassFrame(hourglassFrame_);
    drawProgress();
}

void FirmwareUpdateView::drawComplete()
{
    thefly_display.fillRect(0,
                            kContentY,
                            thefly_display.width(),
                            static_cast<int16_t>(thefly_display.height() - kContentY),
                            TFT_BLACK);

    if (result_ == MicroSdCard::FirmwareUpdateResult::Ok)
    {
        drawTopSprite(sprite_thumbsup_100, SPRITE_THUMBSUP_100_BYTES, SPRITE_THUMBSUP_100_WIDTH, SPRITE_THUMBSUP_100_HEIGHT);
        drawMessage("Firmware update ready\nTouch screen to reboot", 224);
        return;
    }

    drawTopSprite(sprite_xcircle_100, SPRITE_XCIRCLE_100_BYTES, SPRITE_XCIRCLE_100_WIDTH, SPRITE_XCIRCLE_100_HEIGHT);

    char text[160];
    snprintf(text, sizeof(text), "Firmware update failed\n%s\nTouch screen to reboot", failureText());
    drawMessage(text, 224);
}

void FirmwareUpdateView::drawTopSprite(const uint8_t* data, size_t bytes, uint32_t width, uint32_t height)
{
    if (!data || bytes == 0 || width == 0 || height == 0)
    {
        return;
    }

    SpriteDraw::drawPng(data, bytes, centered_x(width), kTopSpriteY, width, height, true);
}

void FirmwareUpdateView::drawMessage(const char* text, int16_t maxY)
{
    thefly_display.fillRect(kTextX,
                            kTextY,
                            kTextWidth,
                            static_cast<int16_t>(maxY - kTextY),
                            TFT_BLACK);
    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextDatum(top_left);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    FlyGuiTextUtil::drawWrappedText(text ? text : "", kTextX, kTextY, kTextWidth, maxY, kTextLineHeight);
}

void FirmwareUpdateView::drawHourglassFrame(uint8_t frame)
{
    const int16_t x = centered_x(kHourglassSize);
    thefly_display.fillRect(x, kHourglassY, kHourglassSize, kHourglassSize, TFT_BLACK);

    switch (frame % 3)
    {
    case 0:
        SpriteDraw::drawPng(sprite_hourglass_60_1, SPRITE_HOURGLASS_60_1_BYTES, x, kHourglassY, SPRITE_HOURGLASS_60_1_WIDTH, SPRITE_HOURGLASS_60_1_HEIGHT, true);
        break;
    case 1:
        SpriteDraw::drawPng(sprite_hourglass_60_2, SPRITE_HOURGLASS_60_2_BYTES, x, kHourglassY, SPRITE_HOURGLASS_60_2_WIDTH, SPRITE_HOURGLASS_60_2_HEIGHT, true);
        break;
    default:
        SpriteDraw::drawPng(sprite_hourglass_60_3, SPRITE_HOURGLASS_60_3_BYTES, x, kHourglassY, SPRITE_HOURGLASS_60_3_WIDTH, SPRITE_HOURGLASS_60_3_HEIGHT, true);
        break;
    }
}

void FirmwareUpdateView::drawProgress()
{
    progressBar_.relocate(progress_x(), kProgressY, kProgressWidth, kProgressHeight);
    progressBar_.update(progressPercent());

    char writtenText[20];
    char totalText[20];
    format_bytes(bytesWritten_, writtenText, sizeof(writtenText));
    format_bytes(bytesTotal_, totalText, sizeof(totalText));

    char text[56];
    snprintf(text, sizeof(text), "%u%% %s/%s", static_cast<unsigned>(roundedProgressPercent()), writtenText, totalText);

    thefly_display.fillRect(0, kProgressTextY, thefly_display.width(), kProgressTextHeight, TFT_BLACK);
    thefly_display.setTextFont(kTextFont);
    thefly_display.setTextSize(kTextSize);
    thefly_display.setTextColor(TFT_WHITE, TFT_BLACK);
    if (thefly_display.textWidth(text) <= thefly_display.width())
    {
        thefly_display.setTextDatum(top_center);
        thefly_display.drawString(text, thefly_display.width() / 2, kProgressTextY);
        return;
    }

    thefly_display.setTextFont(kSmallTextFont);
    thefly_display.setTextDatum(top_left);
    thefly_display.drawString(text, 1, kProgressTextY);
}

float FirmwareUpdateView::progressPercent() const
{
    if (bytesTotal_ == 0)
    {
        return 0.0f;
    }

    const float percent = (static_cast<float>(bytesWritten_) * 100.0f) / static_cast<float>(bytesTotal_);
    if (percent <= 0.0f)
    {
        return 0.0f;
    }

    return percent >= 100.0f ? 100.0f : percent;
}

uint8_t FirmwareUpdateView::roundedProgressPercent() const
{
    const float percent = progressPercent();
    if (percent <= 0.0f)
    {
        return 0;
    }

    if (percent >= 100.0f)
    {
        return 100;
    }

    return static_cast<uint8_t>(percent + 0.5f);
}

const char* FirmwareUpdateView::failureText() const
{
    switch (result_)
    {
    case MicroSdCard::FirmwareUpdateResult::CardNotReady:
        return "microSD card not ready";
    case MicroSdCard::FirmwareUpdateResult::FileOpenFailed:
        return "firmware.bin could not be opened";
    case MicroSdCard::FirmwareUpdateResult::EmptyFile:
        return "firmware.bin is empty";
    case MicroSdCard::FirmwareUpdateResult::NoUpdatePartition:
        return "no inactive OTA partition";
    case MicroSdCard::FirmwareUpdateResult::FileTooLarge:
        return "firmware.bin is too large";
    case MicroSdCard::FirmwareUpdateResult::OtaBeginFailed:
        return "OTA could not start";
    case MicroSdCard::FirmwareUpdateResult::FileReadFailed:
        return "firmware.bin read failed";
    case MicroSdCard::FirmwareUpdateResult::OtaWriteFailed:
        return "OTA write failed";
    case MicroSdCard::FirmwareUpdateResult::OtaEndFailed:
        return "firmware validation failed";
    case MicroSdCard::FirmwareUpdateResult::SetBootPartitionFailed:
        return "boot partition was not updated";
    case MicroSdCard::FirmwareUpdateResult::Ok:
    default:
        return "unknown error";
    }
}
