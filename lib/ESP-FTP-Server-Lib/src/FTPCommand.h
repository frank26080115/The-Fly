#ifndef FTP_COMMAND_H_
#define FTP_COMMAND_H_

#define MAX_LATENCY 500

#include <Arduino.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <vector>

#include "FTPFilesystem.h"
#include "FTPPath.h"

class FTPCommand {
public:
  FTPCommand(String Name, int ExpectedArgumentCnt, WiFiClient *const Client, FTPFilesystem *const Filesystem = 0, IPAddress *DataAddress = 0, int *DataPort = 0, WiFiServer *PassiveDataServer = 0, bool *PassiveDataMode = 0) : _Name(Name), _ExpectedArgumentCnt(ExpectedArgumentCnt), _Filesystem(Filesystem), _DataAddress(DataAddress), _DataPort(DataPort), _PassiveDataServer(PassiveDataServer), _PassiveDataMode(PassiveDataMode), _Client(Client), _DataConnection(0) {
  }
  virtual ~FTPCommand() {
  }

  String getName() const {
    return _Name;
  }

  virtual void run(FTPPath &WorkDirectory, const std::vector<String> &Line) = 0;

  void SendResponse(int Code, String Text) {
    _Client->print(Code);
    _Client->print(" ");
    _Client->println(Text);
  }

  bool ConnectDataConnection() {
    if (_DataConnection == 0) {
      _DataConnection = new WiFiClient();
    }
    if (_DataConnection->connected()) {
      _DataConnection->stop();
    }
    if (_PassiveDataMode != 0 && *_PassiveDataMode) {
      uint16_t dataLatency = 0;
      while (!_PassiveDataServer->hasClient()) {
        vTaskDelay(1);
        if (dataLatency++ > MAX_LATENCY) {
          SendResponse(425, "No passive data connection");
          return false;
        }
      }
      *_DataConnection = _PassiveDataServer->accept();
    } else {
      _DataConnection->connect(*_DataAddress, *_DataPort);
    }
    if (!_DataConnection->connected()) {
      _DataConnection->stop();
      SendResponse(425, "No data connection");
      return false;
    }
    SendResponse(150, "Accepted data connection");
    return true;
  }

  void data_print(String str) {
    if (_DataConnection == 0 || !_DataConnection->connected()) {
      return;
    }
    _DataConnection->print(str);
  }

  void data_println(String str) {
    if (_DataConnection == 0 || !_DataConnection->connected()) {
      return;
    }
    _DataConnection->println(str);
  }

  void data_send(uint8_t *c, size_t l) {
    if (_DataConnection == 0 || !_DataConnection->connected()) {
      return;
    }
    _DataConnection->write(c, l);
  }

  int data_read(uint8_t *c, size_t l) {
    if (_DataConnection != 0) {
      if (_DataConnection->available() > 0){
        return _DataConnection->readBytes(c, l);
  
      }else{
  
        uint16_t DataLatency=0;
        while(!(_DataConnection->available() > 0)){
          vTaskDelay(1);
          if(DataLatency++ > MAX_LATENCY){
            return 0;
          }
        }
        return _DataConnection->readBytes(c, l);
      }
    }
    return 0;
  }

  void CloseDataConnection() {
    _DataConnection->stop();
    if (_PassiveDataMode != 0 && *_PassiveDataMode) {
      _PassiveDataServer->end();
      *_PassiveDataMode = false;
    }
  }

protected:
  static String fileName(FsFile &file) {
    char name[256] = {0};
    return file.getName(name, sizeof(name)) == 0 ? String() : String(name);
  }

  IPAddress ControlLocalIP() const {
    return _Client->localIP();
  }

  String               _Name;
  int                  _ExpectedArgumentCnt;
  FTPFilesystem *const _Filesystem;
  IPAddress *const     _DataAddress;
  int *const           _DataPort;
  WiFiServer *const    _PassiveDataServer;
  bool *const          _PassiveDataMode;

private:
  WiFiClient *const _Client;
  WiFiClient       *_DataConnection;
};

class FTPCommandTransfer : public FTPCommand {
public:
  FTPCommandTransfer(String Name, int ExpectedArgumentCnt, WiFiClient *const Client, FTPFilesystem *const Filesystem = 0, IPAddress *DataAddress = 0, int *DataPort = 0, WiFiServer *PassiveDataServer = 0, bool *PassiveDataMode = 0) : FTPCommand(Name, ExpectedArgumentCnt, Client, Filesystem, DataAddress, DataPort, PassiveDataServer, PassiveDataMode) {
  }

  virtual void workOnData() = 0;

  bool trasferInProgress() {
    return _file;
  }

  void abort() {
    if (_file) {
      CloseDataConnection();
      SendResponse(426, "Transfer aborted");
      _file.close();
    }
  }

protected:
  FsFile _file;
};

#endif
