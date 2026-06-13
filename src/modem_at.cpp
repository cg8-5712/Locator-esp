#include "modem_at.h"

namespace locator {

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

bool ModemAtClient::sendCommand(const String& command, uint32_t timeoutMs) {
  if (stream_ == nullptr || commandPending_) {
    return false;
  }

  pendingKind_ = PendingKind::kLineCommand;
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

bool ModemAtClient::sendSocketData(uint8_t linkNumber, const String& payload, uint32_t timeoutMs) {
  if (stream_ == nullptr || commandPending_ || payload.length() == 0) {
    return false;
  }

  pendingKind_ = PendingKind::kSocketSend;
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
  completedCommand_.command = pendingCommand_;
  completedCommand_.response = pendingResponse_;
  completedCommand_.success = success;
  completedCommand_.timedOut = timedOut;
  hasCompletedCommand_ = true;

  if (success && pendingCommand_ == "AT") {
    status_.atResponsive = true;
  }
  if (success && pendingCommand_ == "ATE0") {
    status_.echoDisabled = true;
  }

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

  parseStatusLine(line);

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
  if (line == "RDY") {
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
  }
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
