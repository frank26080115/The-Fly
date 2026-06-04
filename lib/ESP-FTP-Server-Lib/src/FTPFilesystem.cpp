#ifdef NO_GLOBAL_INSTANCES
#include <logger.h>
#endif

#include "FTPFilesystem.h"
#include "espftp_common.h"

FTPFilesystem::BusyScope::BusyScope(FTPFilesystem *Filesystem) : _Filesystem(Filesystem) {
  if (_Filesystem) {
    _Filesystem->beginBusy();
  }
}

FTPFilesystem::BusyScope::~BusyScope() {
  if (_Filesystem) {
    _Filesystem->endBusy();
  }
}

FTPFilesystem::FTPFilesystem() : _BusyDepth(0) {
}

FTPFilesystem::~FTPFilesystem() {
}

void FTPFilesystem::addFilesystem(String Name, SdFs *const Filesystem) {
  _Filesystems[Name] = Filesystem;
}

void FTPFilesystem::clearFilesystemList() {
  _Filesystems.clear();
}

FsFile FTPFilesystem::open(const String &path, oflag_t mode) {
  BusyScope busy(this);
  SdFs *fs = getFilesystem(path);
  if (fs == 0) {
    return FsFile();
  }
  return fs->open(getPathWithoutFS(path), mode);
}

bool FTPFilesystem::exists(const String &path) {
  BusyScope busy(this);
  SdFs *fs = getFilesystem(path);
  if (fs == 0) {
    return false;
  }
  return fs->exists(getPathWithoutFS(path));
}

bool FTPFilesystem::remove(const String &path) {
  BusyScope busy(this);
  SdFs *fs = getFilesystem(path);
  if (fs == 0) {
    return false;
  }
  return fs->remove(getPathWithoutFS(path));
}

bool FTPFilesystem::rename(const String &pathFrom, const String &pathTo) {
  BusyScope busy(this);
  SdFs *fsFrom = getFilesystem(pathFrom);
  SdFs *fsTo   = getFilesystem(pathTo);
  if (fsFrom == 0 || fsTo == 0) {
    return false;
  }
  if (fsFrom != fsTo) {
    // cant move/rename from one filesystem to another one!
    return false;
  }
  return fsFrom->rename(getPathWithoutFS(pathFrom), getPathWithoutFS(pathTo));
}

bool FTPFilesystem::mkdir(const String &path) {
  BusyScope busy(this);
  SdFs *fs = getFilesystem(path);
  if (fs == 0) {
    return false;
  }
  return fs->mkdir(getPathWithoutFS(path));
}

bool FTPFilesystem::rmdir(const String &path) {
  BusyScope busy(this);
  SdFs *fs = getFilesystem(path);
  if (fs == 0) {
    return false;
  }
  return fs->rmdir(getPathWithoutFS(path));
}

bool FTPFilesystem::isBusy() const {
  return _BusyDepth > 0;
}

void FTPFilesystem::printFilesystems() {
  for (auto const &fs : _Filesystems) {
#ifndef NO_GLOBAL_INSTANCES
    Serial.println(fs.first);
#else
    logPrintlnI(fs.first);
#endif
  }
}

SdFs *FTPFilesystem::getFilesystem(String path) {
  if (_Filesystems.empty()) {
    return 0;
  }

  std::list<String>                    splitted = FTPPath::splitPath(path);
  if (splitted.empty() || _Filesystems.size() == 1) {
    return _Filesystems.begin()->second;
  }

  String                               name     = *(splitted.begin());
  std::map<String, SdFs *>::iterator iter        = _Filesystems.find(name);
  if (iter == _Filesystems.end()) {
#ifndef NO_GLOBAL_INSTANCES
    Serial.println("[ERROR] Filesystem not found!");
#else
    logPrintlnE("[ERROR] Filesystem not found!");
#endif
    return 0;
  }
  return iter->second;
}

String FTPFilesystem::getPathWithoutFS(String path) {
  std::list<String> splitted = FTPPath::splitPath(path);
  if (!splitted.empty() && (_Filesystems.size() != 1 || _Filesystems.find(splitted.front()) != _Filesystems.end())) {
    splitted.pop_front();
  }
  String path_without = FTPPath::createPath(splitted);
  return path_without;
}

void FTPFilesystem::beginBusy() {
  ++_BusyDepth;
}

void FTPFilesystem::endBusy() {
  if (_BusyDepth > 0) {
    --_BusyDepth;
  }
}
