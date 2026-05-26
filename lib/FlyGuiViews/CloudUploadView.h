#pragma once

#include "CloudUpload.h"
#include "ConnWaitingView.h"

class CloudUploadView : public ConnWaitingView
{
public:
    explicit CloudUploadView(FlyGuiItemCallback cancelCallback);

    void configureUpload(CloudUpload* uploader, const char* targetName);
    void updateProgress(const CloudUpload::Status& status);

protected:
    void drawBottomCenter() override;
    bool updateHourglass(uint32_t now, bool forced) override;

private:
    void drawProgress();
    uint8_t progressPercent() const;
    uint16_t progressColor() const;

    CloudUpload*        uploader_ = nullptr;
    CloudUpload::Status status_ = {};
};
