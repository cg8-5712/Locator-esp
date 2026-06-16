#include "app_controller.h"

#include "config.h"

namespace locator {

namespace {

constexpr ModemOp kInitOperations[] = {
    ModemOp::kAt,
    ModemOp::kQueryImei,
    ModemOp::kQueryFirmwareVersion,
    ModemOp::kQueryNetworkRegistration,
    ModemOp::kQuerySignalQuality,
    ModemOp::kQueryIccid,
    ModemOp::kQuerySimSlot,
};

constexpr ModemOp kPeriodicOperations[] = {
    ModemOp::kAt,
    ModemOp::kQueryImei,
    ModemOp::kQueryFirmwareVersion,
    ModemOp::kQueryNetworkRegistration,
    ModemOp::kQuerySignalQuality,
    ModemOp::kQueryIccid,
    ModemOp::kQuerySimSlot,
    ModemOp::kMqttQueryStatus,
};

constexpr size_t kInitOperationCount = sizeof(kInitOperations) / sizeof(kInitOperations[0]);
constexpr size_t kPeriodicOperationCount =
    sizeof(kPeriodicOperations) / sizeof(kPeriodicOperations[0]);
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
    case ModemOp::kQuerySignalQuality:
      return F("query signal quality");
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
    case ModemOp::kQuerySignalQuality:
      return modemClient.querySignalQuality();
    case ModemOp::kQueryIccid:
      return modemClient.queryIccid();
    case ModemOp::kQuerySimSlot:
      return modemClient.querySimSlot();
    case ModemOp::kMqttQueryStatus:
      return modemClient.mqttQueryStatus();
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
  initCommandIndex_ = 0;
  mqttCommandIndex_ = 0;
  periodicCommandIndex_ = 0;
  initRetryCount_ = 0;
  mqttRetryCount_ = 0;
  lastPublishAtMs_ = 0;
  gpsStartedLogged_ = false;
  gpsUnableLogged_ = false;

  debug_.println(F("[APP] Boot delay started"));
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
        debug_.println(F("[INIT][DEVICE] Starting device initialization"));
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
        debug_.println(F("[APP] Leaving recovery, restarting modem init"));
        state_ = State::kInitModem;
        stateEnteredAtMs_ = now;
        initCommandIndex_ = 0;
        initRetryCount_ = 0;
        mqttCommandIndex_ = 0;
        mqttRetryCount_ = 0;
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
  if (!gpsStartedLogged_ && gpsState != GpsState::kNotStarted) {
    debug_.println(F("[GPS] Startup success: data stream detected"));
    gpsStartedLogged_ = true;
  }

  if (gpsParser_.hasNewFix()) {
    lastFix_ = gpsParser_.takeLatestFix();
    hasLastFix_ = true;
    gpsUnableLogged_ = false;

    debug_.print(F("[GPS] Valid GNGLL: "));
    debug_.println(lastFix_.rawSentence);
    debug_.print(F("[GPS] Compact payload: "));
    debug_.println(lastFix_.compactPayload);
    lastGpsHeartbeatAtMs_ = millis();
    return;
  }

  if (!gpsUnableLogged_ && gpsState == GpsState::kUnableToLocate) {
    debug_.println(F("[GPS] Unable to locate within timeout, will publish zero payload"));
    gpsUnableLogged_ = true;
  }

  if (millis() - lastGpsHeartbeatAtMs_ >= config::kGpsHeartbeatIntervalMs) {
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
    debug_.print(F("[MODEM] URC: "));
    debug_.println(urc);
  }

  AtCommandResult result;
  while (modemClient_.takeCompletedCommand(result)) {
    handleCompletedCommand(result);
  }
}

void AppController::handleCompletedCommand(const AtCommandResult& result) {
  debug_.print(F("[MODEM] "));
  debug_.print(result.command);
  debug_.print(F(" => "));
  debug_.println(result.success ? F("OK") : (result.timedOut ? F("TIMEOUT") : F("ERROR")));

  if (result.response.length() != 0) {
    debug_.println(result.response);
  }

  if (state_ == State::kInitModem) {
    if (result.success) {
      initRetryCount_ = 0;
      initCommandIndex_++;

      if (initCommandIndex_ >= kInitOperationCount) {
        if (modemClient_.status().startupReady) {
          beginMqttInitialization();
        } else {
          debug_.println(
              F("[INIT][DEVICE] Waiting startup URCs: RDY/SIM_SUCCESS/NETWORK_ACTIVATE_SUCCESS"));
        }
      }
      return;
    }

    initRetryCount_++;
    if (initRetryCount_ <= 2) {
      debug_.print(F("[INIT][DEVICE] Retrying step, attempt "));
      debug_.println(initRetryCount_ + 1);
      return;
    }

    enterRecovery(F("device init failed"));
    return;
  }

  if (state_ == State::kInitMqtt) {
    bool mqttStepSucceeded = shouldTreatMqttInitResultAsSuccess(result);
    if (result.op == ModemOp::kMqttQueryStatus &&
        mqttStepSucceeded &&
        !modemClient_.status().mqttConnected) {
      mqttStepSucceeded = false;
      debug_.println(F("[INIT][MQTT] Connection status is not ready yet"));
    }

    if (mqttStepSucceeded) {
      mqttRetryCount_ = 0;
      mqttCommandIndex_++;

      if (mqttCommandIndex_ >= kMqttInitStepCount) {
        state_ = State::kRunning;
        stateEnteredAtMs_ = millis();
        lastPublishAtMs_ = 0;
        debug_.println(F("[INIT][MQTT] MQTT connection initialization finished"));
        debug_.println(F("[APP] Initialization complete, entering running state"));
      }
      return;
    }

    mqttRetryCount_++;
    if (mqttRetryCount_ <= 2) {
      debug_.print(F("[INIT][MQTT] Retrying step, attempt "));
      debug_.println(mqttRetryCount_ + 1);
      return;
    }

    enterRecovery(F("mqtt init failed"));
    return;
  }

  if (state_ == State::kRunning && !result.success) {
    if (result.op == ModemOp::kAt) {
      debug_.println(F("[APP] Health check failed"));
    } else if (result.op == ModemOp::kQueryNetworkRegistration ||
        result.op == ModemOp::kQuerySignalQuality) {
      debug_.println(F("[APP] Periodic modem query failed"));
    }
  }
}

void AppController::logModemSummary() {
  const ModemStatus& status = modemClient_.status();

  debug_.print(F("[STATUS] startup="));
  debug_.print(status.startupReady ? F("ready") : F("waiting"));
  debug_.print(F(" health="));
  debug_.print(status.healthCheckOk ? F("ok") : F("fail"));
  debug_.print(F(" gps="));
  debug_.print(gpsStateLabel(currentGpsState()));
  debug_.print(F(" net="));
  debug_.print(status.networkRegistered ? F("ready") : F("waiting"));
  debug_.print(F(" creg="));
  debug_.print(status.cregStat);
  debug_.print(F(" csq="));
  debug_.print(status.csqRssi);
  debug_.print(F(","));
  debug_.print(status.csqBer);
  debug_.print(F(" sim="));
  debug_.print(status.simSlot);
  debug_.print(F(" mqtt="));
  debug_.print(status.mqttStatus);
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

  if (hasLastFix_) {
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
  debug_.println(F("[INIT][DEVICE] Device initialization finished"));
  debug_.println(F("[INIT][MQTT] Starting MQTT connection initialization"));
}

void AppController::requestNextInitCommand() {
  if (!modemClient_.isIdle() || initCommandIndex_ >= kInitOperationCount) {
    return;
  }

  const ModemOp op = kInitOperations[initCommandIndex_];
  debug_.print(F("[INIT][DEVICE] Step: "));
  debug_.println(deviceInitStepLabel(op));
  if (!startModemOperation(modemClient_, op)) {
    enterRecovery(F("cannot send init command"));
  }
}

void AppController::requestNextMqttCommand() {
  if (!modemClient_.isIdle()) {
    return;
  }

  debug_.print(F("[INIT][MQTT] Step: "));
  debug_.println(mqttInitStepLabel(mqttCommandIndex_));

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
  static uint32_t lastPeriodicRequestAtMs = 0;

  if (!modemClient_.isIdle()) {
    return;
  }

  const uint32_t now = millis();
  const bool shouldPublish = now - lastPublishAtMs_ >= config::kMqttPublishIntervalMs;
  if (shouldPublish && modemClient_.status().mqttConnected) {
    const String payload = buildLocationPayload();
    if (modemClient_.mqttPublish(config::kMqttLocationTopic, payload)) {
      lastPublishAtMs_ = now;
      return;
    }

    enterRecovery(F("cannot publish MQTT payload"));
    return;
  }

  if (now - lastPeriodicRequestAtMs < 5000) {
    return;
  }

  lastPeriodicRequestAtMs = now;
  const ModemOp op = kPeriodicOperations[periodicCommandIndex_];
  periodicCommandIndex_ = (periodicCommandIndex_ + 1) % kPeriodicOperationCount;

  if (!startModemOperation(modemClient_, op)) {
    enterRecovery(F("cannot send periodic command"));
  }
}

void AppController::enterRecovery(const __FlashStringHelper* reason) {
  debug_.print(F("[APP] Recovery: "));
  debug_.println(reason);
  state_ = State::kRecovery;
  stateEnteredAtMs_ = millis();
  initCommandIndex_ = 0;
  initRetryCount_ = 0;
  mqttCommandIndex_ = 0;
  mqttRetryCount_ = 0;
}

bool AppController::shouldTreatMqttInitResultAsSuccess(const AtCommandResult& result) const {
  if (result.success) {
    return true;
  }

  if (result.op == ModemOp::kMqttConfigureBroker && result.response.indexOf("ERROR=202") >= 0) {
    debug_.println(F("[INIT][MQTT] Broker already exists, continuing with existing server"));
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
    return String(F("0,0,0"));
  }

  return lastFix_.compactPayload;
}

String AppController::buildMqttInitTestPayload() const {
  String payload = "test+";
  payload += config::kDeviceName;
  return payload;
}

}  // namespace locator
