#include "debug_monitor.h"

#include "config.h"

#include <cstring>
#include <cstdio>

namespace locator {

namespace {

uint8_t hexNibble(char c) {
  if (c >= '0' && c <= '9') {
    return static_cast<uint8_t>(c - '0');
  }
  if (c >= 'A' && c <= 'F') {
    return static_cast<uint8_t>(c - 'A' + 10);
  }
  if (c >= 'a' && c <= 'f') {
    return static_cast<uint8_t>(c - 'a' + 10);
  }
  return 0;
}

}  // namespace

DebugMonitorApp::DebugMonitorApp(Stream& debug, Stream& gpsStream, Stream& modemStream)
    : debug_(debug), gpsStream_(gpsStream), modemStream_(modemStream) {}

void DebugMonitorApp::begin() {
  debug_.println(F("[MON] UART debug monitor"));
  debug_.print(F("[MON] target="));
  if (config::kMonitorTarget == config::MonitorTarget::kGpsOnly) {
    debug_.println(F("GPS_ONLY"));
  } else if (config::kMonitorTarget == config::MonitorTarget::kModemOnly) {
    debug_.println(F("MODEM_ONLY"));
  } else {
    debug_.println(F("BOTH"));
  }
  debug_.println(F("[MON] RX traffic is printed as escaped text and HEX."));
  if (config::kMonitorTarget != config::MonitorTarget::kGpsOnly) {
    debug_.println(F("[MON] Type an AT command in the USB serial monitor and press Enter."));
    debug_.println(F("[MON] ESP32 will append <CR><LF> before sending to the modem."));
    debug_.println(F("[MON] Auto test: modem passes when an auto-sent AT<CR><LF> returns OK."));
  }
  if (config::kMonitorTarget != config::MonitorTarget::kModemOnly) {
    debug_.println(F("[MON] Auto test: GPS passes on the first valid ASCII/NMEA line."));
  }
  lastHeartbeatAtMs_ = millis();
  startedAtMs_ = lastHeartbeatAtMs_;
  lastAtProbeAtMs_ = 0;
  gpsPassed_ = false;
  modemPassed_ = false;
  waitingForAtOk_ = false;
}

void DebugMonitorApp::poll() {
  if (config::kMonitorTarget != config::MonitorTarget::kGpsOnly) {
    pollUsbInput();
    pollModem();
    runAutoTests();
  }

  if (config::kMonitorTarget != config::MonitorTarget::kModemOnly) {
    pollGps();
  }

  const uint32_t now = millis();
  if (now - lastHeartbeatAtMs_ >= 10000) {
    debug_.print(F("[MON] alive"));
    if (config::kMonitorTarget != config::MonitorTarget::kModemOnly) {
      debug_.print(F(" gps="));
      debug_.print(gpsPassed_ ? F("PASS") : F("WAIT"));
    }
    if (config::kMonitorTarget != config::MonitorTarget::kGpsOnly) {
      debug_.print(F(" modem="));
      debug_.print(modemPassed_ ? F("PASS") : (waitingForAtOk_ ? F("WAIT_OK") : F("WAIT")));
    }
    debug_.println();
    lastHeartbeatAtMs_ = now;
  }
}

void DebugMonitorApp::pollUsbInput() {
  while (debug_.available() > 0) {
    const char c = static_cast<char>(debug_.read());

    if (c == '\r' || c == '\n') {
      if (droppingUsbLine_) {
        droppingUsbLine_ = false;
        usbLineLength_ = 0;
        continue;
      }

      if (usbLineLength_ == 0) {
        continue;
      }

      usbLineBuffer_[usbLineLength_] = '\0';
      processUsbLine();
      usbLineLength_ = 0;
      continue;
    }

    appendChar(c, usbLineBuffer_, sizeof(usbLineBuffer_), usbLineLength_, droppingUsbLine_);
  }
}

void DebugMonitorApp::pollGps() {
  while (gpsStream_.available() > 0) {
    const char c = static_cast<char>(gpsStream_.read());

    if (c == '\r' || c == '\n') {
      if (droppingGpsLine_) {
        droppingGpsLine_ = false;
        gpsLineLength_ = 0;
        continue;
      }

      if (gpsLineLength_ == 0) {
        processGpsTerminator(c);
        continue;
      }

      gpsLineBuffer_[gpsLineLength_] = '\0';
      processGpsLine(gpsLineBuffer_, gpsLineLength_);
      processGpsTerminator(c);
      gpsLineLength_ = 0;
      continue;
    }

    appendChar(c, gpsLineBuffer_, sizeof(gpsLineBuffer_), gpsLineLength_, droppingGpsLine_);
  }
}

void DebugMonitorApp::pollModem() {
  while (modemStream_.available() > 0) {
    const char c = static_cast<char>(modemStream_.read());

    if (c == '>') {
      const char prompt[] = ">";
      printEscapedLine(debug_, F("[MODEM-RX]"), prompt, 1);
      continue;
    }

    if (c == '\r' || c == '\n') {
      if (droppingModemLine_) {
        droppingModemLine_ = false;
        modemLineLength_ = 0;
        continue;
      }

      if (modemLineLength_ == 0) {
        processModemTerminator(c);
        continue;
      }

      modemLineBuffer_[modemLineLength_] = '\0';
      processModemLine(modemLineBuffer_, modemLineLength_);
      processModemTerminator(c);
      modemLineLength_ = 0;
      continue;
    }

    appendChar(
        c,
        modemLineBuffer_,
        sizeof(modemLineBuffer_),
        modemLineLength_,
        droppingModemLine_);
  }
}

void DebugMonitorApp::processUsbLine() {
  char txBuffer[kLineBufferSize + 2] = {};
  memcpy(txBuffer, usbLineBuffer_, usbLineLength_);
  txBuffer[usbLineLength_] = '\r';
  txBuffer[usbLineLength_ + 1] = '\n';

  printEscapedLine(debug_, F("[MODEM-TX]"), txBuffer, usbLineLength_ + 2);
  modemStream_.write(reinterpret_cast<const uint8_t*>(txBuffer), usbLineLength_ + 2);
}

void DebugMonitorApp::processGpsLine(const char* line, size_t length) {
  printEscapedLine(debug_, F("[GPS-RX]"), line, length);

  if (!gpsPassed_ && isPrintableAsciiLine(line, length)) {
    gpsPassed_ = true;
    debug_.println(F("[GPS-TEST] PASS: received a printable ASCII line from GPS UART"));
  }

  if (length >= 7 && strncmp(line, "$GNGLL,", 7) == 0) {
    uint8_t calculated = 0;
    uint8_t expected = 0;
    const bool valid = validateNmea(String(line), calculated, expected);

    char checksumText[32] = {};
    snprintf(
        checksumText,
        sizeof(checksumText),
        "[GPS-GNGLL] checksum %s calc=%02X expected=%02X",
        valid ? "OK" : "FAIL",
        calculated,
        expected);
    debug_.println(checksumText);
  }
}

void DebugMonitorApp::processModemLine(const char* line, size_t length) {
  printEscapedLine(debug_, F("[MODEM-RX]"), line, length);

  String normalized(line);
  normalized.trim();
  if (waitingForAtOk_ && normalized == "OK") {
    waitingForAtOk_ = false;
    if (!modemPassed_) {
      modemPassed_ = true;
      debug_.println(F("[MODEM-TEST] PASS: AT<CR><LF> returned OK"));
    }
  }
}

void DebugMonitorApp::processGpsTerminator(char c) {
  const char term[1] = {c};
  printEscapedLine(debug_, F("[GPS-RX-TERM]"), term, 1);
}

void DebugMonitorApp::processModemTerminator(char c) {
  const char term[1] = {c};
  printEscapedLine(debug_, F("[MODEM-RX-TERM]"), term, 1);
}

void DebugMonitorApp::runAutoTests() {
  if (modemPassed_) {
    return;
  }

  const uint32_t now = millis();
  if (now - startedAtMs_ < 2000) {
    return;
  }

  if (waitingForAtOk_ && now - lastAtProbeAtMs_ < 2000) {
    return;
  }

  if (now - lastAtProbeAtMs_ < 5000) {
    return;
  }

  const char probe[] = {'A', 'T', '\r', '\n'};
  printEscapedLine(debug_, F("[MODEM-TX]"), probe, sizeof(probe));
  modemStream_.write(reinterpret_cast<const uint8_t*>(probe), sizeof(probe));
  waitingForAtOk_ = true;
  lastAtProbeAtMs_ = now;
}

bool DebugMonitorApp::appendChar(
    char c,
    char* buffer,
    size_t capacity,
    size_t& length,
    bool& droppingLine) {
  if (droppingLine) {
    return false;
  }

  if (length >= capacity - 1) {
    droppingLine = true;
    length = 0;
    return false;
  }

  buffer[length++] = c;
  return true;
}

String DebugMonitorApp::escapeBytes(const char* data, size_t length) {
  String out;
  out.reserve(length * 4);

  for (size_t i = 0; i < length; ++i) {
    const uint8_t byte = static_cast<uint8_t>(data[i]);
    if (byte >= 32 && byte <= 126 && byte != '\\') {
      out += static_cast<char>(byte);
      continue;
    }

    if (byte == '\\') {
      out += "\\\\";
      continue;
    }

    char escaped[5] = {};
    snprintf(escaped, sizeof(escaped), "\\x%02X", byte);
    out += escaped;
  }

  return out;
}

String DebugMonitorApp::hexBytes(const char* data, size_t length) {
  String out;
  out.reserve(length * 3);

  for (size_t i = 0; i < length; ++i) {
    char hex[4] = {};
    snprintf(hex, sizeof(hex), "%02X", static_cast<uint8_t>(data[i]));
    if (i > 0) {
      out += ' ';
    }
    out += hex;
  }

  return out;
}

bool DebugMonitorApp::isPrintableAsciiLine(const char* data, size_t length) {
  if (length == 0) {
    return false;
  }

  for (size_t i = 0; i < length; ++i) {
    const uint8_t byte = static_cast<uint8_t>(data[i]);
    if (byte < 32 || byte > 126) {
      return false;
    }
  }

  return true;
}

bool DebugMonitorApp::validateNmea(const String& line, uint8_t& calculated, uint8_t& expected) {
  calculated = 0;
  expected = 0;

  if (!line.startsWith("$")) {
    return false;
  }

  const int asteriskIndex = line.indexOf('*');
  if (asteriskIndex <= 1 || asteriskIndex + 2 >= line.length()) {
    return false;
  }

  for (int i = 1; i < asteriskIndex; ++i) {
    calculated ^= static_cast<uint8_t>(line[i]);
  }

  expected = static_cast<uint8_t>(
      (hexNibble(line[asteriskIndex + 1]) << 4) | hexNibble(line[asteriskIndex + 2]));
  return calculated == expected;
}

void DebugMonitorApp::printEscapedLine(
    Stream& debug,
    const __FlashStringHelper* prefix,
    const char* data,
    size_t length) {
  debug.print(prefix);
  debug.print(F(" text=\""));
  debug.print(escapeBytes(data, length));
  debug.print(F("\" hex="));
  debug.println(hexBytes(data, length));
}

}  // namespace locator
