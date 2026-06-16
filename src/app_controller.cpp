#include "app_controller.h"

#include "config.h"

namespace locator {

namespace {

struct CommandDefinition {
  const char* command;
  uint32_t timeoutMs;
};

constexpr CommandDefinition kInitCommands[] = {
    {"AT", 1000},
    {"ATE0", 1000},
    {"AT+CREG?", 1500},
    {"AT+CSQ", 1500},
    {"AT+QCCID", 2000},
    {"AT+SINGLESIM?", 1500},
};

constexpr CommandDefinition kPeriodicCommands[] = {
    {"AT+CREG?", 1500},
    {"AT+CSQ", 1500},
    {"AT+QCCID", 2000},
    {"AT+SINGLESIM?", 1500},
    {"AT+QMTSTATU", 2000},
};

constexpr size_t kInitCommandCount = sizeof(kInitCommands) / sizeof(kInitCommands[0]);
constexpr size_t kPeriodicCommandCount = sizeof(kPeriodicCommands) / sizeof(kPeriodicCommands[0]);

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
        debug_.println(F("[APP] Starting modem init"));
      }
      break;

    case State::kInitModem:
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

  if (gpsParser_.hasNewFix()) {
    lastFix_ = gpsParser_.takeLatestFix();
    hasLastFix_ = true;

    debug_.print(F("[GPS] Valid GNGLL: "));
    debug_.println(lastFix_.rawSentence);
    debug_.print(F("[GPS] Compact payload: "));
    debug_.println(lastFix_.compactPayload);
    lastGpsHeartbeatAtMs_ = millis();
    return;
  }

  if (millis() - lastGpsHeartbeatAtMs_ >= config::kGpsHeartbeatIntervalMs) {
    const GpsStats& stats = gpsParser_.stats();
    debug_.print(F("[GPS] Waiting fix. lines="));
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

      if (initCommandIndex_ >= kInitCommandCount) {
        state_ = State::kInitMqtt;
        stateEnteredAtMs_ = millis();
        mqttCommandIndex_ = 0;
        mqttRetryCount_ = 0;
        debug_.println(F("[APP] Modem init finished, starting MQTT init"));
      }
      return;
    }

    initRetryCount_++;
    if (initRetryCount_ <= 2) {
      debug_.print(F("[APP] Retrying init command, attempt "));
      debug_.println(initRetryCount_ + 1);
      return;
    }

    enterRecovery(F("init failed"));
    return;
  }

  if (state_ == State::kInitMqtt) {
    if (result.success) {
      mqttRetryCount_ = 0;
      mqttCommandIndex_++;

      if (mqttCommandIndex_ >= 4) {
        state_ = State::kRunning;
        stateEnteredAtMs_ = millis();
        lastPublishAtMs_ = 0;
        debug_.println(F("[APP] MQTT init finished"));
      }
      return;
    }

    mqttRetryCount_++;
    if (mqttRetryCount_ <= 2) {
      debug_.print(F("[APP] Retrying MQTT command, attempt "));
      debug_.println(mqttRetryCount_ + 1);
      return;
    }

    enterRecovery(F("mqtt init failed"));
    return;
  }

  if (state_ == State::kRunning && !result.success) {
    if (result.command == "AT+CREG?" || result.command == "AT+CSQ") {
      debug_.println(F("[APP] Periodic modem query failed"));
    }
  }
}

void AppController::logModemSummary() {
  const ModemStatus& status = modemClient_.status();

  debug_.print(F("[STATUS] net="));
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

  if (hasLastFix_) {
    debug_.print(F(" last_fix="));
    debug_.print(lastFix_.compactPayload);
  }

  debug_.println();
}

void AppController::requestNextInitCommand() {
  if (!modemClient_.isIdle() || initCommandIndex_ >= kInitCommandCount) {
    return;
  }

  const CommandDefinition& cmd = kInitCommands[initCommandIndex_];
  if (!modemClient_.sendCommand(cmd.command, cmd.timeoutMs)) {
    enterRecovery(F("cannot send init command"));
  }
}

void AppController::requestNextMqttCommand() {
  if (!modemClient_.isIdle()) {
    return;
  }

  String command;
  uint32_t timeoutMs = 3000;

  switch (mqttCommandIndex_) {
    case 0:
      command = "AT+QMTCFG=\"";
      command += config::kMqttClientId;
      command += "\",\"";
      command += config::kMqttUsername;
      command += "\",\"";
      command += config::kMqttPassword;
      command += "\"";
      break;

    case 1:
      command = "AT+QMTCONNCFG=\"";
      command += config::kMqttBroker;
      command += "\",";
      command += String(config::kMqttPort);
      command += ",";
      command += String(config::kMqttAutoReconnect ? 1 : 0);
      break;

    case 2:
      command = "AT+QMTSTART=";
      command += String(config::kMqttCleanSession);
      command += ",";
      command += String(config::kMqttKeepAliveSeconds);
      break;

    case 3:
      command = "AT+QMTSTATU";
      timeoutMs = 2000;
      break;

    default:
      return;
  }

  if (!modemClient_.sendCommand(command, timeoutMs)) {
    enterRecovery(F("cannot send MQTT init command"));
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
    String command = "AT+QMTPUB=\"";
    command += config::kMqttLocationTopic;
    command += "\",0,0,\"";
    command += payload;
    command += "\"";

    if (modemClient_.sendCommand(command, 3000)) {
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
  const CommandDefinition& cmd = kPeriodicCommands[periodicCommandIndex_];
  periodicCommandIndex_ = (periodicCommandIndex_ + 1) % kPeriodicCommandCount;

  if (!modemClient_.sendCommand(cmd.command, cmd.timeoutMs)) {
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

String AppController::buildLocationPayload() const {
  if (!hasLastFix_) {
    return String();
  }

  if (millis() - gpsParser_.lastValidFixAtMs() > config::kGpsStaleAfterMs) {
    return String();
  }

  return lastFix_.compactPayload;
}

}  // namespace locator
