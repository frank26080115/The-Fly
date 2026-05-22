#ifndef MSLD_H_
#define MSLD_H_
// Copyright (c) 2020 Arkady Korotkevich
// Based on LIST.h by Peter Buchegger
#include <WiFiClient.h>

#include "../FTPCommand.h"
#include "../common.h"
#include <time.h>

class MLSD : public FTPCommand {
public:
  explicit MLSD(WiFiClient *const Client, FTPFilesystem *const Filesystem, IPAddress *DataAddress, int *DataPort, WiFiServer *PassiveDataServer, bool *PassiveDataMode) : FTPCommand("MLSD", 1, Client, Filesystem, DataAddress, DataPort, PassiveDataServer, PassiveDataMode) {
  }

  void run(FTPPath &WorkDirectory, const std::vector<String> &Line) override {
    if (!ConnectDataConnection()) {
      return;
    }

    FsFile root = _Filesystem->open(WorkDirectory.getPath());

    if (!root || !root.isDirectory()) {
      root.close();
      CloseDataConnection();
      SendResponse(550, "Can't open directory " + WorkDirectory.getPath());
      return;
    }

    int  cnt = 0;
    FsFile f   = root.openNextFile();
    while (f) {
      // type=
      data_print("type=");
      if (f.isDirectory())
        data_print("dir;");
      else
        data_print("file;");

      // size=
      if (f.isDirectory())
        data_print("sizd=");
      else
        data_print("size=");
      data_print(String(f.size()));
      data_print(";");

      // modify=YYYYMMDDHHMMSS; // GMT (!!!!)
      char     buf[128];
      uint16_t date = 0;
      uint16_t time = 0;
      if (f.getModifyDateTime(&date, &time)) {
        sprintf(buf, "modify=%4d%02d%02d%02d%02d%02d;", FS_YEAR(date), FS_MONTH(date), FS_DAY(date), FS_HOUR(time), FS_MINUTE(time), FS_SECOND(time));
      } else {
        sprintf(buf, "modify=19700101000000;");
      }
      data_print(String(buf));

      // UNIX.mode=
      data_print("UNIX.mode=0700;");

      // <filename>
      data_print(" ");
      String filename = fileName(f);
      data_println(filename);

      cnt++;
      f = root.openNextFile();
    }

    root.close();
    CloseDataConnection();
    SendResponse(226, String(cnt) + " matches total");
  }
};

#endif
