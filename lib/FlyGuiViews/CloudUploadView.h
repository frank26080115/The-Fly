#pragma once

#include "../FlyGui/ProgressBar.h"
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
    void    drawProgress();
    float   progressPercent() const;
    uint8_t roundedProgressPercent() const;

    CloudUpload*        uploader_ = nullptr;
    CloudUpload::Status status_   = {};
    ProgressBar         progressBar_;
};
