#include "modem_at.h"

namespace locator {

const char* modemOpName(ModemOp op) {
  switch (op) {
    case ModemOp::kNone:
      return "none";
    case ModemOp::kAt:
      return "ping";
    case ModemOp::kQueryImei:
      return "query_imei";
    case ModemOp::kQueryFirmwareVersion:
      return "query_firmware_version";
    case ModemOp::kQueryNetworkRegistration:
      return "query_network_registration";
    case ModemOp::kQuerySignalQuality:
      return "query_signal_quality";
    case ModemOp::kQueryIccid:
      return "query_iccid";
    case ModemOp::kQuerySimSlot:
      return "query_sim_slot";
    case ModemOp::kMqttConfigureCredentials:
      return "mqtt_configure_credentials";
    case ModemOp::kMqttConfigureBroker:
      return "mqtt_configure_broker";
    case ModemOp::kMqttStart:
      return "mqtt_start";
    case ModemOp::kMqttQueryStatus:
      return "mqtt_query_status";
    case ModemOp::kMqttPublish:
      return "mqtt_publish";
    case ModemOp::kSocketSend:
      return "socket_send";
  }

  return "unknown";
}

void ModemAtClient::begin(Stream& stream) {
  stream_ = &stream;
}

void ModemAtClient::poll() {
  if (stream_ == nullptr) {
    return;
  }

  while (stream_->available() > 0) {
    const char c = static_cast<char>(stream_->read());

    if (commandPending_ && pendingKind_ == PendingKind::kSocketSend &&
        waitingForSendPrompt_ && c == '>') {
      if (pendingResponse_.length() != 0) {
        pendingResponse_ += "\n";
      }
      pendingResponse_ += ">";
      stream_->print(pendingPayload_);
      waitingForSendPrompt_ = false;
      continue;
    }

    if (c == '\r' || c == '\n') {
      if (droppingLine_) {
        droppingLine_ = false;
        lineLength_ = 0;
        continue;
      }

      if (lineLength_ == 0) {
        continue;
      }

      lineBuffer_[lineLength_] = '\0';
      handleLine(String(lineBuffer_));
      lineLength_ = 0;
      continue;
    }

    if (droppingLine_) {
      continue;
    }

    if (lineLength_ >= kLineBufferSize - 1) {
      droppingLine_ = true;
      lineLength_ = 0;
      continue;
    }

    lineBuffer_[lineLength_++] = c;
  }

  if (commandPending_ && millis() - commandStartedAtMs_ > commandTimeoutMs_) {
    completeCurrentCommand(false, true);
  }
}

bool ModemAtClient::ping() {
  return startCommand(ModemOp::kAt, "AT", 1000);
}

bool ModemAtClient::queryImei() {
  return startCommand(ModemOp::kQueryImei, "AT+GSN", 2000);
}

bool ModemAtClient::queryFirmwareVersion() {
  return startCommand(ModemOp::kQueryFirmwareVersion, "AT+GMR", 2000);
}

bool ModemAtClient::queryNetworkRegistration() {
  return startCommand(ModemOp::kQueryNetworkRegistration, "AT+CREG?", 1500);
}

bool ModemAtClient::querySignalQuality() {
  return startCommand(ModemOp::kQuerySignalQuality, "AT+CSQ", 1500);
}

bool ModemAtClient::queryIccid() {
  return startCommand(ModemOp::kQueryIccid, "AT+QCCID", 2000);
}

bool ModemAtClient::querySimSlot() {
  return startCommand(ModemOp::kQuerySimSlot, "AT+SINGLESIM?", 1500);
}

bool ModemAtClient::mqttConfigureCredentials(
    const char* clientId,
    const char* username,
    const char* password) {
  String command = "AT+QMTCFG=\"";
  command += clientId;
  command += "\",\"";
  command += username;
  command += "\",\"";
  command += password;
  command += "\"";
  return startCommand(ModemOp::kMqttConfigureCredentials, command, 3000);
}

bool ModemAtClient::mqttConfigureBroker(const char* host, uint16_t port, bool autoReconnect) {
  String command = "AT+QMTCONNCFG=\"";
  command += host;
  command += "\",";
  command += String(port);
  command += ",";
  command += String(autoReconnect ? 1 : 0);
  return startCommand(ModemOp::kMqttConfigureBroker, command, 3000);
}

bool ModemAtClient::mqttStart(uint8_t cleanSession, uint16_t keepAliveSeconds) {
  String command = "AT+QMTSTART=";
  command += String(cleanSession);
  command += ",";
  command += String(keepAliveSeconds);
  return startCommand(ModemOp::kMqttStart, command, 3000);
}

bool ModemAtClient::mqttQueryStatus() {
  return startCommand(ModemOp::kMqttQueryStatus, "AT+QMTSTATU", 2000);
}

bool ModemAtClient::mqttPublish(
    const char* topic,
    const String& payload,
    uint8_t qos,
    bool retain) {
  String command = "AT+QMTPUB=\"";
  command += topic;
  command += "\",";
  command += String(qos);
  command += ",";
  command += String(retain ? 1 : 0);
  command += ",\"";
  command += payload;
  command += "\"";
  return startCommand(ModemOp::kMqttPublish, command, 3000);
}

bool ModemAtClient::startCommand(ModemOp op, const String& command, uint32_t timeoutMs) {
  if (stream_ == nullptr || commandPending_) {
    return false;
  }

  pendingKind_ = PendingKind::kLineCommand;
  pendingOp_ = op;
  pendingCommand_ = command;
  pendingResponse_.remove(0);
  pendingPayload_.remove(0);
  waitingForSendPrompt_ = false;
  commandStartedAtMs_ = millis();
  commandTimeoutMs_ = timeoutMs;
  commandPending_ = true;

  stream_->print(command);
  stream_->print("\r\n");
  return true;
}

bool ModemAtClient::socketSend(uint8_t linkNumber, const String& payload, uint32_t timeoutMs) {
  if (stream_ == nullptr || commandPending_ || payload.length() == 0) {
    return false;
  }

  pendingKind_ = PendingKind::kSocketSend;
  pendingOp_ = ModemOp::kSocketSend;
  pendingPayload_ = payload;
  pendingCommand_ = "AT+QISEND=" + String(linkNumber) + "," + String(payload.length());
  pendingResponse_.remove(0);
  waitingForSendPrompt_ = true;
  commandStartedAtMs_ = millis();
  commandTimeoutMs_ = timeoutMs;
  commandPending_ = true;

  stream_->print(pendingCommand_);
  stream_->print("\r\n");
  return true;
}

bool ModemAtClient::isIdle() const {
  return !commandPending_;
}

bool ModemAtClient::takeCompletedCommand(AtCommandResult& outResult) {
  if (!hasCompletedCommand_) {
    return false;
  }

  outResult = completedCommand_;
  hasCompletedCommand_ = false;
  completedCommand_.op = ModemOp::kNone;
  completedCommand_.command.remove(0);
  completedCommand_.response.remove(0);
  completedCommand_.success = false;
  completedCommand_.timedOut = false;
  return true;
}

bool ModemAtClient::takeUrc(String& outUrc) {
  if (!hasUrc_) {
    return false;
  }

  outUrc = lastUrc_;
  hasUrc_ = false;
  lastUrc_.remove(0);
  return true;
}

const ModemStatus& ModemAtClient::status() const {
  return status_;
}

void ModemAtClient::completeCurrentCommand(bool success, bool timedOut) {
  completedCommand_.op = pendingOp_;
  completedCommand_.command = pendingCommand_;
  completedCommand_.response = pendingResponse_;
  completedCommand_.success = success;
  completedCommand_.timedOut = timedOut;
  hasCompletedCommand_ = true;

  if (pendingOp_ == ModemOp::kAt) {
    if (success) {
      status_.healthCheckOk = true;
      status_.lastHealthCheckAtMs = millis();
      status_.lastUpdateMs = status_.lastHealthCheckAtMs;
    } else {
      status_.healthCheckOk = false;
    }
  }

  pendingOp_ = ModemOp::kNone;
  pendingCommand_.remove(0);
  pendingResponse_.remove(0);
  pendingPayload_.remove(0);
  commandPending_ = false;
  pendingKind_ = PendingKind::kNone;
  waitingForSendPrompt_ = false;
  commandStartedAtMs_ = 0;
  commandTimeoutMs_ = 0;
}

void ModemAtClient::handleLine(const String& line) {
  if (line.length() == 0) {
    return;
  }

  String normalizedLine = line;
  normalizedLine.trim();
  parseStatusLine(normalizedLine);

  if (commandPending_) {
    if (pendingOp_ == ModemOp::kQueryImei) {
      parseImei(normalizedLine);
    } else if (pendingOp_ == ModemOp::kQueryFirmwareVersion) {
      parseFirmwareVersion(normalizedLine);
    }
  }

  if (commandPending_ && normalizedLine == pendingCommand_) {
    return;
  }

  if (commandPending_) {
    if (pendingKind_ == PendingKind::kSocketSend) {
      if (normalizedLine == "ERROR" || normalizedLine.startsWith("ERROR=") ||
          normalizedLine == "SEND FAIL" || normalizedLine.startsWith("+CME ERROR") ||
          normalizedLine.startsWith("+CMS ERROR")) {
        if (pendingResponse_.length() != 0) {
          pendingResponse_ += "\n";
        }
        pendingResponse_ += normalizedLine;
        completeCurrentCommand(false, false);
        return;
      }

      if (normalizedLine == "SENDOK" || normalizedLine == "SEND OK") {
        if (pendingResponse_.length() != 0) {
          pendingResponse_ += "\n";
        }
        pendingResponse_ += normalizedLine;
        completeCurrentCommand(true, false);
        return;
      }

      if (normalizedLine == "OK" && waitingForSendPrompt_) {
        if (pendingResponse_.length() != 0) {
          pendingResponse_ += "\n";
        }
        pendingResponse_ += normalizedLine;
        return;
      }

      if (pendingResponse_.length() != 0) {
        pendingResponse_ += "\n";
      }
      pendingResponse_ += normalizedLine;
      return;
    }

    if (normalizedLine == "OK") {
      completeCurrentCommand(true, false);
      return;
    }

    if (normalizedLine == "ERROR" || normalizedLine.startsWith("ERROR=") ||
        normalizedLine.startsWith("+CME ERROR") ||
        normalizedLine.startsWith("+CMS ERROR")) {
      if (pendingResponse_.length() != 0) {
        pendingResponse_ += "\n";
      }
      pendingResponse_ += normalizedLine;
      completeCurrentCommand(false, false);
      return;
    }

    if (pendingResponse_.length() != 0) {
      pendingResponse_ += "\n";
    }
    pendingResponse_ += normalizedLine;
    return;
  }

  lastUrc_ = normalizedLine;
  hasUrc_ = true;
}

void ModemAtClient::parseStatusLine(const String& line) {
  if (line.endsWith("RDY")) {
    status_.sawRdy = true;
    refreshStartupReady();
    status_.lastUpdateMs = millis();
    return;
  }

  if (line == "SIM_SUCCESS") {
    status_.simReady = true;
    refreshStartupReady();
    status_.lastUpdateMs = millis();
    return;
  }

  if (line == "NETWORK_ACTIVATE_SUCCESS") {
    status_.networkActivated = true;
    refreshStartupReady();
    status_.lastUpdateMs = millis();
    return;
  }

  if (line.startsWith("+CREG:")) {
    parseCreg(line);
    return;
  }

  if (line.startsWith("+CSQ:")) {
    parseCsq(line);
    return;
  }

  if (line.startsWith("+QICCID:") || line.startsWith("+QCCID:")) {
    parseIccid(line);
    return;
  }

  if (line.startsWith("+SINGLESIM:")) {
    parseSimSlot(line);
    return;
  }

  if (line.startsWith("+QMTSTATU:")) {
    const int colonIndex = line.indexOf(':');
    if (colonIndex < 0) {
      return;
    }

    String payload = line.substring(colonIndex + 1);
    payload.trim();
    status_.mqttStatus = payload.toInt();
    status_.mqttConnected = status_.mqttStatus == 1;
    status_.lastUpdateMs = millis();
  }
}

void ModemAtClient::refreshStartupReady() {
  status_.startupReady = status_.sawRdy && status_.simReady && status_.networkActivated;
}

void ModemAtClient::parseImei(const String& line) {
  if (line.length() == 0 || line == "OK" || line == "ERROR" || line.startsWith("ERROR=") ||
      line.startsWith("+CME ERROR") || line.startsWith("+CMS ERROR")) {
    return;
  }

  bool allDigits = true;
  for (size_t i = 0; i < line.length(); ++i) {
    if (!isDigit(line[i])) {
      allDigits = false;
      break;
    }
  }

  if (!allDigits) {
    return;
  }

  status_.imei = line;
  status_.lastUpdateMs = millis();
}

void ModemAtClient::parseFirmwareVersion(const String& line) {
  if (line.length() == 0 || line == "OK" || line == "ERROR" || line.startsWith("ERROR=") ||
      line.startsWith("+CME ERROR") || line.startsWith("+CMS ERROR")) {
    return;
  }

  status_.firmwareVersion = line;
  status_.lastUpdateMs = millis();
}

void ModemAtClient::parseCreg(const String& line) {
  const int colonIndex = line.indexOf(':');
  if (colonIndex < 0) {
    return;
  }

  String payload = line.substring(colonIndex + 1);
  payload.trim();

  const int commaIndex = payload.indexOf(',');
  if (commaIndex < 0) {
    return;
  }

  status_.cregMode = payload.substring(0, commaIndex).toInt();

  int nextCommaIndex = payload.indexOf(',', commaIndex + 1);
  if (nextCommaIndex < 0) {
    nextCommaIndex = payload.length();
  }

  status_.cregStat = payload.substring(commaIndex + 1, nextCommaIndex).toInt();
  status_.networkRegistered = status_.cregStat == 1 || status_.cregStat == 5;
  status_.lastUpdateMs = millis();
}

void ModemAtClient::parseCsq(const String& line) {
  const int colonIndex = line.indexOf(':');
  if (colonIndex < 0) {
    return;
  }

  String payload = line.substring(colonIndex + 1);
  payload.trim();

  const int commaIndex = payload.indexOf(',');
  if (commaIndex < 0) {
    return;
  }

  status_.csqRssi = payload.substring(0, commaIndex).toInt();
  status_.csqBer = payload.substring(commaIndex + 1).toInt();
  status_.lastUpdateMs = millis();
}

void ModemAtClient::parseIccid(const String& line) {
  const int colonIndex = line.indexOf(':');
  if (colonIndex < 0) {
    return;
  }

  status_.iccid = line.substring(colonIndex + 1);
  status_.iccid.trim();
  status_.lastUpdateMs = millis();
}

void ModemAtClient::parseSimSlot(const String& line) {
  const int colonIndex = line.indexOf(':');
  if (colonIndex < 0) {
    return;
  }

  String payload = line.substring(colonIndex + 1);
  payload.trim();
  status_.simSlot = payload.toInt();
  status_.lastUpdateMs = millis();
}

}  // namespace locator
