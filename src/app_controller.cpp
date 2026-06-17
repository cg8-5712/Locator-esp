#include "app_controller.h"

#include "config.h"

namespace locator {

namespace {

constexpr float kEarthRadiusMeters = 6371000.0f;

constexpr ModemOp kInitOperations[] = {
    ModemOp::kAt,
    ModemOp::kQueryImei,
    ModemOp::kQueryFirmwareVersion,
    ModemOp::kQueryNetworkRegistration,
    ModemOp::kQueryIccid,
    ModemOp::kQuerySimSlot,
};

constexpr size_t kInitOperationCount = sizeof(kInitOperations) / sizeof(kInitOperations[0]);
constexpr size_t kMqttInitStepCount = 5;

const __FlashStringHelper* deviceInitStepLabel(ModemOp op) {
  switch (op) {
    case ModemOp::kAt:
      return F("AT health check");
    case ModemOp::kQueryImei:
      return F("query IMEI");
    case ModemOp::kQueryFirmwareVersion:
      return F("query firmware version");
    case ModemOp::kQueryNetworkRegistration:
      return F("query network registration");
    case ModemOp::kQueryIccid:
      return F("query ICCID");
    case ModemOp::kQuerySimSlot:
      return F("query SIM slot");
    default:
      return F("unknown");
  }
}

const __FlashStringHelper* mqttInitStepLabel(size_t stepIndex) {
  switch (stepIndex) {
    case 0:
      return F("register client info");
    case 1:
      return F("configure broker info");
    case 2:
      return F("start MQTT session");
    case 3:
      return F("verify MQTT connection");
    case 4:
      return F("publish startup test message");
    default:
      return F("unknown");
  }
}

bool startModemOperation(ModemAtClient& modemClient, ModemOp op) {
  switch (op) {
    case ModemOp::kAt:
      return modemClient.ping();
    case ModemOp::kQueryImei:
      return modemClient.queryImei();
    case ModemOp::kQueryFirmwareVersion:
      return modemClient.queryFirmwareVersion();
    case ModemOp::kQueryNetworkRegistration:
      return modemClient.queryNetworkRegistration();
    case ModemOp::kQueryIccid:
      return modemClient.queryIccid();
    case ModemOp::kQuerySimSlot:
      return modemClient.querySimSlot();
    default:
      return false;
  }
}

}  // namespace

AppController::AppController(Stream& debug, Stream& gpsStream, Stream& modemStream)
    : debug_(debug) {
  gpsParser_.begin(gpsStream);
  modemClient_.begin(modemStream);
}

void AppController::begin() {
  state_ = State::kBootDelay;
  stateEnteredAtMs_ = millis();
  lastStatusPrintAtMs_ = millis();
  lastGpsHeartbeatAtMs_ = millis();
  lastHealthCheckAtMs_ = 0;
  initCommandIndex_ = 0;
  mqttCommandIndex_ = 0;
  initRetryCount_ = 0;
  mqttRetryCount_ = 0;
  lastPublishAtMs_ = 0;
  lastFullReportAtMs_ = 0;
  lastStillReportAtMs_ = 0;
  lastNoFixReportAtMs_ = 0;
  stationarySinceMs_ = 0;
  lastReportKind_ = ReportKind::kNone;
  gpsStartedLogged_ = false;
  gpsUnableLogged_ = false;
  hasLastFix_ = false;
  hasLastReportedFix_ = false;

  if (isDetailedLoggingEnabled()) {
    debug_.println(F("[APP] Boot delay started"));
  }
}

void AppController::poll() {
  handleGps();
  handleModem();

  const uint32_t now = millis();

  switch (state_) {
    case State::kBootDelay:
      if (now - stateEnteredAtMs_ >= config::kBootDelayMs) {
        state_ = State::kInitModem;
        stateEnteredAtMs_ = now;
        if (isDetailedLoggingEnabled()) {
          debug_.println(F("[INIT][DEVICE] Starting device initialization"));
        }
      }
      break;

    case State::kInitModem:
      if (initCommandIndex_ >= kInitOperationCount) {
        if (modemClient_.status().startupReady) {
          beginMqttInitialization();
        }
        break;
      }
      requestNextInitCommand();
      break;

    case State::kInitMqtt:
      requestNextMqttCommand();
      break;

    case State::kRunning:
      requestNextPeriodicCommand();
      break;

    case State::kRecovery:
      if (now - stateEnteredAtMs_ >= config::kRecoveryDelayMs) {
        if (isDetailedLoggingEnabled()) {
          debug_.println(F("[APP] Leaving recovery, restarting modem init"));
        }
        state_ = State::kInitModem;
        stateEnteredAtMs_ = now;
        initCommandIndex_ = 0;
        initRetryCount_ = 0;
        mqttCommandIndex_ = 0;
        mqttRetryCount_ = 0;
        lastHealthCheckAtMs_ = 0;
        lastFullReportAtMs_ = 0;
        lastStillReportAtMs_ = 0;
        lastNoFixReportAtMs_ = 0;
        stationarySinceMs_ = 0;
        lastReportKind_ = ReportKind::kNone;
        hasLastFix_ = false;
        hasLastReportedFix_ = false;
      }
      break;
  }

  if (now - lastStatusPrintAtMs_ >= config::kStatusPrintIntervalMs) {
    logModemSummary();
    lastStatusPrintAtMs_ = now;
  }
}

void AppController::handleGps() {
  gpsParser_.poll();

  const GpsState gpsState = currentGpsState();
  if (isDetailedLoggingEnabled() && !gpsStartedLogged_ && gpsState != GpsState::kNotStarted) {
    debug_.println(F("[GPS] Startup success: data stream detected"));
    gpsStartedLogged_ = true;
  }

  if (gpsParser_.hasNewFix()) {
    lastFix_ = gpsParser_.takeLatestFix();
    hasLastFix_ = true;
    gpsUnableLogged_ = false;

    if (stationarySinceMs_ == 0 || !hasLastReportedFix_ || hasMovementBeyondThreshold()) {
      stationarySinceMs_ = millis();
    }

    if (isDetailedLoggingEnabled()) {
      debug_.print(F("[GPS] Valid GNGLL: "));
      debug_.println(lastFix_.rawSentence);
      debug_.print(F("[GPS] Compact payload: "));
      debug_.println(lastFix_.compactPayload);
    }
    lastGpsHeartbeatAtMs_ = millis();
    return;
  }

  if (isDetailedLoggingEnabled() && !gpsUnableLogged_ && gpsState == GpsState::kUnableToLocate) {
    debug_.println(F("[GPS] Unable to locate within timeout, will publish zero payload"));
    gpsUnableLogged_ = true;
  }

  if (isDetailedLoggingEnabled() &&
      millis() - lastGpsHeartbeatAtMs_ >= config::kGpsHeartbeatIntervalMs) {
    const GpsStats& stats = gpsParser_.stats();
    debug_.print(F("[GPS] state="));
    debug_.print(gpsStateLabel(gpsState));
    debug_.print(F(" lines="));
    debug_.print(stats.totalLines);
    debug_.print(F(" gngll="));
    debug_.print(stats.totalGngll);
    debug_.print(F(" valid="));
    debug_.print(stats.validFixes);
    debug_.print(F(" cksum_err="));
    debug_.print(stats.checksumErrors);
    debug_.print(F(" invalid="));
    debug_.println(stats.invalidFixes);
    lastGpsHeartbeatAtMs_ = millis();
  }
}

void AppController::handleModem() {
  modemClient_.poll();

  String urc;
  while (modemClient_.takeUrc(urc)) {
    if (isDetailedLoggingEnabled()) {
      debug_.print(F("[MODEM] URC: "));
      debug_.println(urc);
    }
  }

  AtCommandResult result;
  while (modemClient_.takeCompletedCommand(result)) {
    handleCompletedCommand(result);
  }
}

void AppController::handleCompletedCommand(const AtCommandResult& result) {
  if (isDetailedLoggingEnabled()) {
    debug_.print(F("[MODEM] "));
    debug_.print(result.command);
    debug_.print(F(" => "));
    debug_.println(result.success ? F("OK") : (result.timedOut ? F("TIMEOUT") : F("ERROR")));

    if (result.response.length() != 0) {
      debug_.println(result.response);
    }
  }

  if (state_ == State::kInitModem) {
    if (result.success) {
      initRetryCount_ = 0;
      initCommandIndex_++;

      if (initCommandIndex_ >= kInitOperationCount) {
        if (modemClient_.status().startupReady) {
          beginMqttInitialization();
        } else if (isDetailedLoggingEnabled()) {
          debug_.println(
              F("[INIT][DEVICE] Waiting startup URCs: RDY/SIM_SUCCESS/NETWORK_ACTIVATE_SUCCESS"));
        }
      }
      return;
    }

    initRetryCount_++;
    if (initRetryCount_ <= 2) {
      if (isDetailedLoggingEnabled()) {
        debug_.print(F("[INIT][DEVICE] Retrying step, attempt "));
        debug_.println(initRetryCount_ + 1);
      }
      return;
    }

    logInitFailure(F("DEVICE"), F("failed"));
    enterRecovery(F("device init failed"));
    return;
  }

  if (state_ == State::kInitMqtt) {
    bool mqttStepSucceeded = shouldTreatMqttInitResultAsSuccess(result);
    if (result.op == ModemOp::kMqttQueryStatus &&
        mqttStepSucceeded &&
        !modemClient_.status().mqttConnected) {
      mqttStepSucceeded = false;
      if (isDetailedLoggingEnabled()) {
        debug_.println(F("[INIT][MQTT] Connection status is not ready yet"));
      }
    }

    if (mqttStepSucceeded) {
      mqttRetryCount_ = 0;
      mqttCommandIndex_++;

      if (mqttCommandIndex_ >= kMqttInitStepCount) {
        state_ = State::kRunning;
        stateEnteredAtMs_ = millis();
        lastPublishAtMs_ = 0;
        logInitSuccess(F("MQTT"), F("success"));
        if (isDetailedLoggingEnabled()) {
          debug_.println(F("[APP] Initialization complete, entering running state"));
        }
      }
      return;
    }

    mqttRetryCount_++;
    if (mqttRetryCount_ <= 2) {
      if (isDetailedLoggingEnabled()) {
        debug_.print(F("[INIT][MQTT] Retrying step, attempt "));
        debug_.println(mqttRetryCount_ + 1);
      }
      return;
    }

    logInitFailure(F("MQTT"), F("failed"));
    enterRecovery(F("mqtt init failed"));
    return;
  }

  if (isDetailedLoggingEnabled() && state_ == State::kRunning && !result.success) {
    if (result.op == ModemOp::kAt) {
      debug_.println(F("[APP] Health check failed"));
    }
  }
}

void AppController::logModemSummary() {
  const ModemStatus& status = modemClient_.status();

  debug_.print(F("[STATUS] build="));
  debug_.print(config::kFirmwareVersion);
  debug_.print(F(" startup="));
  debug_.print(status.startupReady ? F("ready") : F("waiting"));
  debug_.print(F(" health="));
  debug_.print(status.healthCheckOk ? F("ok") : F("fail"));
  debug_.print(F(" gps="));
  debug_.print(gpsStateLabel(currentGpsState()));
  debug_.print(F(" net="));
  debug_.print(status.networkRegistered ? F("ready") : F("waiting"));
  debug_.print(F(" mqtt="));
  debug_.print(status.mqttStatus);

  if (isDetailedLoggingEnabled()) {
    debug_.print(F(" creg="));
    debug_.print(status.cregStat);
    debug_.print(F(" csq="));
    debug_.print(status.csqRssi);
    debug_.print(F(","));
    debug_.print(status.csqBer);
    debug_.print(F(" sim="));
    debug_.print(status.simSlot);
    debug_.print(F(" iccid="));
    if (status.iccid.length() == 0) {
      debug_.print(F("N/A"));
    } else {
      debug_.print(status.iccid);
    }
    debug_.print(F(" imei="));
    if (status.imei.length() == 0) {
      debug_.print(F("N/A"));
    } else {
      debug_.print(status.imei);
    }
    debug_.print(F(" fw="));
    if (status.firmwareVersion.length() == 0) {
      debug_.print(F("N/A"));
    } else {
      debug_.print(status.firmwareVersion);
    }
  }

  if (isDetailedLoggingEnabled() && hasLastFix_) {
    debug_.print(F(" last_fix="));
    debug_.print(lastFix_.compactPayload);
  }

  debug_.println();
}

void AppController::beginMqttInitialization() {
  state_ = State::kInitMqtt;
  stateEnteredAtMs_ = millis();
  mqttCommandIndex_ = 0;
  mqttRetryCount_ = 0;
  logInitSuccess(F("DEVICE"), F("success"));
  if (isDetailedLoggingEnabled()) {
    debug_.println(F("[INIT][MQTT] Starting MQTT connection initialization"));
  }
}

void AppController::requestNextInitCommand() {
  if (!modemClient_.isIdle() || initCommandIndex_ >= kInitOperationCount) {
    return;
  }

  if (isDetailedLoggingEnabled()) {
    const ModemOp op = kInitOperations[initCommandIndex_];
    debug_.print(F("[INIT][DEVICE] Step: "));
    debug_.println(deviceInitStepLabel(op));
  }
  const ModemOp op = kInitOperations[initCommandIndex_];
  if (!startModemOperation(modemClient_, op)) {
    enterRecovery(F("cannot send init command"));
  }
}

void AppController::requestNextMqttCommand() {
  if (!modemClient_.isIdle()) {
    return;
  }

  if (isDetailedLoggingEnabled()) {
    debug_.print(F("[INIT][MQTT] Step: "));
    debug_.println(mqttInitStepLabel(mqttCommandIndex_));
  }

  switch (mqttCommandIndex_) {
    case 0:
      if (!modemClient_.mqttConfigureCredentials(
              config::kMqttClientId,
              config::kMqttUsername,
              config::kMqttPassword)) {
        enterRecovery(F("cannot send MQTT init command"));
      }
      return;

    case 1:
      if (!modemClient_.mqttConfigureBroker(
              config::kMqttBroker,
              config::kMqttPort,
              config::kMqttAutoReconnect)) {
        enterRecovery(F("cannot send MQTT init command"));
      }
      return;

    case 2:
      if (!modemClient_.mqttStart(
              config::kMqttCleanSession, config::kMqttKeepAliveSeconds)) {
        enterRecovery(F("cannot send MQTT init command"));
      }
      return;

    case 3:
      if (!modemClient_.mqttQueryStatus()) {
        enterRecovery(F("cannot send MQTT init command"));
      }
      return;

    case 4:
      if (!modemClient_.mqttPublish(config::kMqttTestTopic, buildMqttInitTestPayload())) {
        enterRecovery(F("cannot send MQTT init command"));
      }
      return;

    default:
      return;
  }
}

void AppController::requestNextPeriodicCommand() {
  if (!modemClient_.isIdle()) {
    return;
  }

  const uint32_t now = millis();
  const bool shouldPublish = now - lastPublishAtMs_ >= config::kMqttPublishIntervalMs;
  if (shouldPublish && modemClient_.status().mqttConnected) {
    const ReportKind reportKind = determineNextReportKind(now);
    if (reportKind == ReportKind::kNone) {
      lastPublishAtMs_ = now;
      return;
    }

    const String payload = buildReportPayload(reportKind, now);
    if (modemClient_.mqttPublish(config::kMqttLocationTopic, payload)) {
      lastPublishAtMs_ = now;
      lastReportKind_ = reportKind;

      if (reportKind == ReportKind::kFull) {
        lastFullReportAtMs_ = now;
        lastReportedFix_ = lastFix_;
        hasLastReportedFix_ = true;
      } else if (reportKind == ReportKind::kStill) {
        lastStillReportAtMs_ = now;
      } else if (reportKind == ReportKind::kNoFix) {
        lastNoFixReportAtMs_ = now;
      }
      return;
    }

    enterRecovery(F("cannot publish MQTT payload"));
    return;
  }

  if (now - lastHealthCheckAtMs_ < config::kModemHealthCheckIntervalMs) {
    return;
  }

  if (!modemClient_.ping()) {
    enterRecovery(F("cannot send health check"));
    return;
  }

  lastHealthCheckAtMs_ = now;
}

void AppController::enterRecovery(const __FlashStringHelper* reason) {
  if (isDetailedLoggingEnabled()) {
    debug_.print(F("[APP] Recovery: "));
    debug_.println(reason);
  }
  state_ = State::kRecovery;
  stateEnteredAtMs_ = millis();
  initCommandIndex_ = 0;
  initRetryCount_ = 0;
  mqttCommandIndex_ = 0;
  mqttRetryCount_ = 0;
  lastHealthCheckAtMs_ = 0;
  lastFullReportAtMs_ = 0;
  lastStillReportAtMs_ = 0;
  lastNoFixReportAtMs_ = 0;
  stationarySinceMs_ = 0;
  lastReportKind_ = ReportKind::kNone;
  hasLastFix_ = false;
  hasLastReportedFix_ = false;
}

bool AppController::shouldTreatMqttInitResultAsSuccess(const AtCommandResult& result) const {
  if (result.success) {
    return true;
  }

  if (result.op == ModemOp::kMqttConfigureBroker && result.response.indexOf("ERROR=202") >= 0) {
    if (isDetailedLoggingEnabled()) {
      debug_.println(F("[INIT][MQTT] Broker already exists, continuing with existing server"));
    }
    return true;
  }

  return false;
}

AppController::GpsState AppController::currentGpsState() const {
  if (!gpsParser_.hasSeenAnyData()) {
    return GpsState::kNotStarted;
  }

  if (hasLastFix_ && millis() - gpsParser_.lastValidFixAtMs() <= config::kGpsStaleAfterMs) {
    return GpsState::kLocated;
  }

  if (millis() - gpsParser_.firstDataAtMs() >= config::kGpsUnableToLocateAfterMs) {
    return GpsState::kUnableToLocate;
  }

  return GpsState::kStartedSearching;
}

const __FlashStringHelper* AppController::gpsStateLabel(GpsState state) const {
  switch (state) {
    case GpsState::kNotStarted:
      return F("not_started");
    case GpsState::kStartedSearching:
      return F("searching");
    case GpsState::kLocated:
      return F("located");
    case GpsState::kUnableToLocate:
      return F("unable");
  }

  return F("unknown");
}

String AppController::buildLocationPayload() const {
  if (currentGpsState() != GpsState::kLocated) {
    return String(config::kLocationZeroPayload);
  }

  return lastFix_.compactPayload;
}

AppController::ReportKind AppController::determineNextReportKind(uint32_t now) const {
  const GpsState gpsState = currentGpsState();
  if (gpsState != GpsState::kLocated) {
    if (lastReportKind_ != ReportKind::kNoFix ||
        now - lastNoFixReportAtMs_ >= config::kLocationNoFixKeepaliveMs) {
      return ReportKind::kNoFix;
    }
    return ReportKind::kNone;
  }

  const bool moved = hasMovementBeyondThreshold();
  if (!hasLastReportedFix_ || moved ||
      lastReportKind_ == ReportKind::kNoFix ||
      now - lastFullReportAtMs_ >= config::kLocationFullResyncMs) {
    return ReportKind::kFull;
  }

  if (stationarySinceMs_ != 0 && isWithinStillDistanceThreshold() &&
      now - stationarySinceMs_ >= config::kLocationStillConfirmMs &&
      (lastReportKind_ != ReportKind::kStill ||
       now - lastStillReportAtMs_ >= config::kLocationStillKeepaliveMs)) {
    return ReportKind::kStill;
  }

  return ReportKind::kNone;
}

float AppController::distanceMeters(const GngllData& a, const GngllData& b) const {
  const auto parseDegrees = [](const String& value, char hemisphere) -> float {
    const float raw = value.toFloat();
    const int degrees = static_cast<int>(raw / 100.0f);
    const float minutes = raw - static_cast<float>(degrees) * 100.0f;
    float decimalDegrees = static_cast<float>(degrees) + minutes / 60.0f;
    if (hemisphere == 'S' || hemisphere == 'W') {
      decimalDegrees = -decimalDegrees;
    }
    return decimalDegrees;
  };

  const float lat1 = parseDegrees(a.latitude, a.latitudeHemisphere) * PI / 180.0f;
  const float lon1 = parseDegrees(a.longitude, a.longitudeHemisphere) * PI / 180.0f;
  const float lat2 = parseDegrees(b.latitude, b.latitudeHemisphere) * PI / 180.0f;
  const float lon2 = parseDegrees(b.longitude, b.longitudeHemisphere) * PI / 180.0f;

  const float dLat = lat2 - lat1;
  const float dLon = lon2 - lon1;
  const float sinLat = sinf(dLat / 2.0f);
  const float sinLon = sinf(dLon / 2.0f);
  const float aTerm = sinLat * sinLat + cosf(lat1) * cosf(lat2) * sinLon * sinLon;
  const float cTerm = 2.0f * atan2f(sqrtf(aTerm), sqrtf(1.0f - aTerm));
  return kEarthRadiusMeters * cTerm;
}

bool AppController::hasMovementBeyondThreshold() const {
  if (!hasLastFix_ || !hasLastReportedFix_) {
    return true;
  }

  return distanceMeters(lastFix_, lastReportedFix_) >=
         static_cast<float>(config::kLocationMovementThresholdMeters);
}

bool AppController::isWithinStillDistanceThreshold() const {
  if (!hasLastFix_ || !hasLastReportedFix_) {
    return false;
  }

  return distanceMeters(lastFix_, lastReportedFix_) <=
         static_cast<float>(config::kLocationStillDistanceThresholdMeters);
}

uint32_t AppController::stillDurationSeconds(uint32_t now) const {
  if (stationarySinceMs_ == 0 || now < stationarySinceMs_) {
    return 0;
  }

  return (now - stationarySinceMs_) / 1000;
}

String AppController::buildReportPayload(ReportKind kind, uint32_t now) const {
  switch (kind) {
    case ReportKind::kFull: {
      String payload = config::kReportFullPrefix;
      payload += buildLocationPayload();
      return payload;
    }
    case ReportKind::kStill: {
      String payload = config::kReportStillPrefix;
      payload += String(stillDurationSeconds(now));
      return payload;
    }
    case ReportKind::kNoFix:
      return String(config::kReportNoFixPrefix) + "0";
    case ReportKind::kNone:
    default:
      return String();
  }
}

String AppController::buildMqttInitTestPayload() const {
  String payload = "test+";
  payload += config::kDeviceName;
  return payload;
}

constexpr bool AppController::isDetailedLoggingEnabled() {
  return config::kAppLogLevel == config::AppLogLevel::kDebug;
}

void AppController::logInitSuccess(
    const __FlashStringHelper* scope,
    const __FlashStringHelper* message) {
  debug_.print(F("[INIT]["));
  debug_.print(scope);
  debug_.print(F("] "));
  debug_.println(message);
}

void AppController::logInitFailure(
    const __FlashStringHelper* scope,
    const __FlashStringHelper* message) {
  debug_.print(F("[INIT]["));
  debug_.print(scope);
  debug_.print(F("] "));
  debug_.println(message);
}

}  // namespace locator
