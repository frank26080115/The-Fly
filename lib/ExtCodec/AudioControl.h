#pragma once

#define AUDIO_INPUT_LINEIN 0
#define AUDIO_INPUT_MIC 1

class AudioControl
{
public:
    virtual ~AudioControl() = default;

    virtual bool enable()           = 0;
    virtual bool disable()          = 0;
    virtual bool volume(float n)    = 0;
    virtual bool inputLevel(float n)  = 0;
    virtual bool inputSelect(int n) = 0;
};
