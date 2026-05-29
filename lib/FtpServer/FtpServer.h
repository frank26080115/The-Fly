#pragma once

#include "thefly_common.h"

#include <SdFat.h>

namespace FtpServer
{

bool start(SdFs& microSdFs, const char* username, const char* password);
void poll();
bool started();

} // namespace FtpServer
