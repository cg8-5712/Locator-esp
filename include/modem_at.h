#pragma once

#include <Arduino.h>

namespace locator {

struct AtCommandResult {
  String command;
  String response;
  bool success = false;
  bool timedOut = false;
};

struct ModemStatus {
  bool atResponsive = false;
  bool echoDisabled = false;
  bool networkRegistered = false;
  bool mqttConnected = false;
  int cregMode = -1;
  int cregStat = -1;
  int csqRssi = -1;
  int csqBer = -1;
  int simSlot = -1;
  int mqttStatus = -1;
  String iccid;
  uint32_t lastUpdateMs = 0;
};

class ModemAtClient {
 public:
  void begin(Stream& stream);
  void poll();

  bool sendCommand(const String& command, uint32_t timeoutMs);
  bool sendSocketData(uint8_t linkNumber, const String& payload, uint32_t timeoutMs);
  bool isIdle() const;

  bool takeCompletedCommand(AtCommandResult& outResult);
  bool takeUrc(String& outUrc);

  const ModemStatus& status() const;

 private:
  enum class PendingKind {
    kNone,
    kLineCommand,
    kSocketSend,
  };

  static constexpr size_t kLineBufferSize = 192;

  void completeCurrentCommand(bool success, bool timedOut);
  void handleLine(const String& line);
  void parseStatusLine(const String& line);
  void parseCreg(const String& line);
  void parseCsq(const String& line);
  void parseIccid(const String& line);
  void parseSimSlot(const String& line);

  Stream* stream_ = nullptr;
  char lineBuffer_[kLineBufferSize] = {};
  size_t lineLength_ = 0;
  bool droppingLine_ = false;

  bool commandPending_ = false;
  PendingKind pendingKind_ = PendingKind::kNone;
  String pendingCommand_;
  String pendingResponse_;
  String pendingPayload_;
  bool waitingForSendPrompt_ = false;
  uint32_t commandStartedAtMs_ = 0;
  uint32_t commandTimeoutMs_ = 0;

  bool hasCompletedCommand_ = false;
  AtCommandResult completedCommand_;

  bool hasUrc_ = false;
  String lastUrc_;
  ModemStatus status_;
};

}  // namespace locator
