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

  enum class GpsState {
    kNotStarted,
    kStartedSearching,
    kLocated,
    kUnableToLocate,
  };

  enum class ReportKind {
    kNone,
    kFull,
    kStill,
    kNoFix,
  };

  void handleGps();
  void handleModem();
  void handleCompletedCommand(const AtCommandResult& result);
  bool shouldTreatMqttInitResultAsSuccess(const AtCommandResult& result) const;
  static constexpr bool isDetailedLoggingEnabled();
  void logInitSuccess(const __FlashStringHelper* scope, const __FlashStringHelper* message);
  void logInitFailure(const __FlashStringHelper* scope, const __FlashStringHelper* message);
  void logModemSummary();
  void beginMqttInitialization();
  void requestNextInitCommand();
  void requestNextMqttCommand();
  void requestNextPeriodicCommand();
  void enterRecovery(const __FlashStringHelper* reason);
  GpsState currentGpsState() const;
  const __FlashStringHelper* gpsStateLabel(GpsState state) const;
  ReportKind determineNextReportKind(uint32_t now) const;
  float distanceMeters(const GngllData& a, const GngllData& b) const;
  bool hasMovementBeyondThreshold() const;
  bool isWithinStillDistanceThreshold() const;
  uint32_t stillDurationSeconds(uint32_t now) const;
  String buildLocationPayload() const;
  String buildReportPayload(ReportKind kind, uint32_t now) const;
  String buildMqttInitTestPayload() const;

  Stream& debug_;
  GpsParser gpsParser_;
  ModemAtClient modemClient_;
  State state_ = State::kBootDelay;
  uint32_t stateEnteredAtMs_ = 0;
  uint32_t lastStatusPrintAtMs_ = 0;
  uint32_t lastGpsHeartbeatAtMs_ = 0;
  uint32_t lastHealthCheckAtMs_ = 0;
  size_t initCommandIndex_ = 0;
  size_t mqttCommandIndex_ = 0;
  uint8_t initRetryCount_ = 0;
  uint8_t mqttRetryCount_ = 0;
  uint32_t lastPublishAtMs_ = 0;
  uint32_t lastFullReportAtMs_ = 0;
  uint32_t lastStillReportAtMs_ = 0;
  uint32_t lastNoFixReportAtMs_ = 0;
  uint32_t stationarySinceMs_ = 0;
  ReportKind lastReportKind_ = ReportKind::kNone;

  GngllData lastFix_;
  GngllData lastReportedFix_;
  bool hasLastFix_ = false;
  bool hasLastReportedFix_ = false;
  bool gpsStartedLogged_ = false;
  bool gpsUnableLogged_ = false;
};

}  // namespace locator
