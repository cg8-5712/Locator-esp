#pragma once

#include <Arduino.h>

#include "gps_parser.h"
#include "modem_at.h"

namespace locator {

class AppController {
 public:
  AppController(Stream& debug, Stream& gpsStream, Stream& modemStream);

  void begin();
  void poll();

 private:
  enum class State {
    kBootDelay,
    kInitModem,
    kInitMqtt,
    kRunning,
    kRecovery,
  };

  void handleGps();
  void handleModem();
  void handleCompletedCommand(const AtCommandResult& result);
  void logModemSummary();
  void beginMqttInitialization();
  void requestNextInitCommand();
  void requestNextMqttCommand();
  void requestNextPeriodicCommand();
  void enterRecovery(const __FlashStringHelper* reason);
  String buildLocationPayload() const;
  String buildMqttInitTestPayload() const;

  Stream& debug_;
  GpsParser gpsParser_;
  ModemAtClient modemClient_;
  State state_ = State::kBootDelay;
  uint32_t stateEnteredAtMs_ = 0;
  uint32_t lastStatusPrintAtMs_ = 0;
  uint32_t lastGpsHeartbeatAtMs_ = 0;
  size_t initCommandIndex_ = 0;
  size_t mqttCommandIndex_ = 0;
  size_t periodicCommandIndex_ = 0;
  uint8_t initRetryCount_ = 0;
  uint8_t mqttRetryCount_ = 0;
  uint32_t lastPublishAtMs_ = 0;

  GngllData lastFix_;
  bool hasLastFix_ = false;
};

}  // namespace locator
