#ifndef LIST_H_
#define LIST_H_

#include <WiFiClient.h>

#include "../FTPCommand.h"
#include "../espftp_common.h"

class LIST : public FTPCommand {
public:
  explicit LIST(WiFiClient *const Client, FTPFilesystem *const Filesystem, IPAddress *DataAddress, int *DataPort, WiFiServer *PassiveDataServer, bool *PassiveDataMode) : FTPCommand("LIST", 1, Client, Filesystem, DataAddress, DataPort, PassiveDataServer, PassiveDataMode) {
  }

  void run(FTPPath &WorkDirectory, const std::vector<String> &Line) override {
    if (!ConnectDataConnection()) {
      return;
    }
    FsFile dir = _Filesystem->open(WorkDirectory.getPath()); //
    if (!dir || !dir.isDirectory()) {
      CloseDataConnection();
      SendResponse(550, "Can't open directory " + WorkDirectory.getPath());
      return;
    }
    int  cnt = 0;
    FsFile f   = dir.openNextFile();
    while (f) {
      String filename = fileName(f);
      if (f.isDirectory()) {
        data_print("drwxr-xr-x");
      } else {
        data_print("-rw-r--r--");
      }
      String filesize = String(f.size());
      data_print(" 1 owner group ");
      int fill_cnt = 13 - filesize.length();
      for (int i = 0; i < fill_cnt; i++) {
        data_print(" ");
      }
      data_println(filesize + " Jan 01  1970 " + filename);
      cnt++;
      f.close();
      f = dir.openNextFile();
    }
    CloseDataConnection();
    SendResponse(226, String(cnt) + " matches total");
  }
};

#endif
