#include "FtpServer.h"

#include <ESP-FTP-Server-Lib.h>

namespace FtpServer
{
namespace
{

constexpr const char* kMicroSdMountName = "microSD";

FTPServer g_ftp_server;
bool      g_started = false;

} // namespace

bool start(SdFs& microSdFs, const char* username, const char* password)
{
    if (g_started)
    {
        return true;
    }

    if (!username || username[0] == '\0' || !password || password[0] == '\0')
    {
        return false;
    }

    // This library implements plain FTP, not SFTP or FTPS. The username and
    // password gate access but are not transport encryption.
    g_ftp_server.addUser(username, password);
    g_ftp_server.addFilesystem(kMicroSdMountName, &microSdFs);

    g_started = g_ftp_server.begin();
    return g_started;
}

void poll()
{
    if (g_started)
    {
        g_ftp_server.handle();
    }
}

bool started()
{
    return g_started;
}

bool isBusy()
{
    return g_started && g_ftp_server.isBusy();
}

} // namespace FtpServer
