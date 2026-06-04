#ifndef PASV_H_
#define PASV_H_

#include <WiFiClient.h>

#include "../FTPCommand.h"
#include "../FTPConnection.h"

class PASV : public FTPCommand {
public:
  explicit PASV(WiFiClient *const Client, WiFiServer *PassiveDataServer, bool *PassiveDataMode) : FTPCommand("PASV", 0, Client, 0, 0, 0, PassiveDataServer, PassiveDataMode) {
  }

  void run(FTPPath &WorkDirectory, const std::vector<String> &Line) override {
    _PassiveDataServer->end();
    _PassiveDataServer->begin(FTP_DATA_PORT);
    if (!*_PassiveDataServer) {
      SendResponse(425, "Can't open passive data port");
      return;
    }

    *_PassiveDataMode = true;

    IPAddress local = ControlLocalIP();
    String response = "Entering Passive Mode (";
    response += String(local[0]) + "," + String(local[1]) + "," + String(local[2]) + "," + String(local[3]);
    response += "," + String(FTP_DATA_PORT / 256) + "," + String(FTP_DATA_PORT % 256) + ")";
    SendResponse(227, response);
  }
};

#endif
