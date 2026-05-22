#ifndef FTP_FILESYSTEM_H_
#define FTP_FILESYSTEM_H_

#include <SdFat.h>
#include <map>

#include "FTPPath.h"

class FTPFilesystem {
public:
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

  void printFilesystems();

#ifndef UNIT_TEST
private:
#endif
  SdFs         *getFilesystem(String path);
  String getPathWithoutFS(String path);

  std::map<String, SdFs *> _Filesystems;
};

#endif
