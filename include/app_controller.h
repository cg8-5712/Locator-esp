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
    kOffline,
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

  struct RuntimeConfig {
    uint32_t gpsOfflineAfterMs = 0;
    uint32_t gpsUnableToLocateAfterMs = 0;
    uint32_t modemHealthCheckIntervalMs = 0;
    uint32_t mqttPublishIntervalMs = 0;
    uint32_t locationMovementThresholdMeters = 0;
    uint32_t locationStillDistanceThresholdMeters = 0;
    uint32_t locationStillConfirmMs = 0;
    uint32_t locationStillKeepaliveMs = 0;
    uint32_t locationNoFixKeepaliveMs = 0;
    uint32_t locationFullResyncMs = 0;
    bool remoteConfigOverrideEnabled = false;
  };

  void handleGps();
  void handleModem();
  void handleReceivedMqttMessage(const MqttReceivedMessage& message);
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
  void resetRuntimeConfig();
  bool publishStatus();
  bool publishConfig();
  String buildStatusPayload() const;
  String buildConfigPayload() const;
  bool applyRemoteConfigPayload(const String& payload);
  bool extractJsonUint32(const String& payload, const char* key, uint32_t& outValue) const;
  bool extractJsonBool(const String& payload, const char* key, bool& outValue) const;
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
  RuntimeConfig runtimeConfig_;
  bool shouldPublishStatus_ = false;
  bool shouldPublishConfig_ = false;
  bool mqttCommandTopicSubscribed_ = false;

  GngllData lastFix_;
  GngllData lastReportedFix_;
  bool hasLastFix_ = false;
  bool hasLastReportedFix_ = false;
  bool gpsStartedLogged_ = false;
  bool gpsUnableLogged_ = false;
};

}  // namespace locator
