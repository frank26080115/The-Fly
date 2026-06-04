#ifndef FTP_FILESYSTEM_H_
#define FTP_FILESYSTEM_H_

#include <SdFat.h>
#include <map>

#include "FTPPath.h"

class FTPFilesystem {
public:
  class BusyScope {
  public:
    explicit BusyScope(FTPFilesystem *Filesystem);
    ~BusyScope();

    BusyScope(const BusyScope &) = delete;
    BusyScope &operator=(const BusyScope &) = delete;

  private:
    FTPFilesystem *_Filesystem;
  };

  FTPFilesystem();
  virtual ~FTPFilesystem();

  void addFilesystem(String Name, SdFs *const Filesystem);
  void clearFilesystemList();

  FsFile open(const String &path, oflag_t mode = O_RDONLY);
  bool exists(const String &path);
  bool remove(const String &path);
  bool rename(const String &pathFrom, const String &pathTo);
  bool mkdir(const String &path);
  bool rmdir(const String &path);
  bool isBusy() const;

  void printFilesystems();

#ifndef UNIT_TEST
private:
#endif
  SdFs         *getFilesystem(String path);
  String getPathWithoutFS(String path);
  void beginBusy();
  void endBusy();

  std::map<String, SdFs *> _Filesystems;
  uint32_t _BusyDepth;
};

#endif
