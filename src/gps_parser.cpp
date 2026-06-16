#include "gps_parser.h"

#include <cstdio>
#include <cstring>

namespace locator {

namespace {

uint8_t hexPairToByte(const char high, const char low) {
  const auto decode = [](char c) -> uint8_t {
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
  };

  return static_cast<uint8_t>((decode(high) << 4) | decode(low));
}

}  // namespace

void GpsParser::begin(Stream& stream) {
  stream_ = &stream;
}

void GpsParser::poll() {
  if (stream_ == nullptr) {
    return;
  }

  while (stream_->available() > 0) {
    const char c = static_cast<char>(stream_->read());
    if (!hasSeenAnyData_) {
      hasSeenAnyData_ = true;
      firstDataAtMs_ = millis();
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
      processLine(lineBuffer_);
      lineLength_ = 0;
      continue;
    }

    if (droppingLine_) {
      continue;
    }

    if (lineLength_ >= kLineBufferSize - 1) {
      droppingLine_ = true;
      lineLength_ = 0;
      stats_.overflowLines++;
      continue;
    }

    lineBuffer_[lineLength_++] = c;
  }
}

bool GpsParser::hasNewFix() const {
  return hasNewFix_;
}

GngllData GpsParser::takeLatestFix() {
  hasNewFix_ = false;
  return latestFix_;
}

const GpsStats& GpsParser::stats() const {
  return stats_;
}

uint32_t GpsParser::lastValidFixAtMs() const {
  return lastValidFixAtMs_;
}

bool GpsParser::hasSeenAnyData() const {
  return hasSeenAnyData_;
}

uint32_t GpsParser::firstDataAtMs() const {
  return firstDataAtMs_;
}

bool GpsParser::processLine(const char* line) {
  stats_.totalLines++;

  if (strncmp(line, "$GNGLL,", 7) != 0) {
    return false;
  }

  stats_.totalGngll++;

  GngllData parsedData;
  if (!parseGngllSentence(String(line), parsedData)) {
    return false;
  }

  latestFix_ = parsedData;
  hasNewFix_ = true;
  stats_.validFixes++;
  lastValidFixAtMs_ = millis();
  return true;
}

bool GpsParser::parseGngllSentence(const String& line, GngllData& outData) {
  uint8_t checksum = 0;
  if (!validateNmea(line, checksum)) {
    stats_.checksumErrors++;
    return false;
  }

  const int asteriskIndex = line.indexOf('*');
  if (asteriskIndex < 0) {
    stats_.invalidFixes++;
    return false;
  }

  const String body = line.substring(1, asteriskIndex);
  if (!splitGngllBody(body, outData)) {
    stats_.invalidFixes++;
    return false;
  }

  outData.rawSentence = line;
  outData.checksum = checksum;

  char checksumText[3] = {};
  snprintf(checksumText, sizeof(checksumText), "%02X", checksum);
  outData.compactPayload = outData.latitude;
  outData.compactPayload += outData.latitudeHemisphere;
  outData.compactPayload += ",";
  outData.compactPayload += outData.longitude;
  outData.compactPayload += outData.longitudeHemisphere;
  outData.compactPayload += ",";
  outData.compactPayload += outData.utcTimeHhmmss;
  outData.compactPayload += outData.status;
  outData.compactPayload += outData.mode;
  outData.compactPayload += "*";
  outData.compactPayload += checksumText;
  return true;
}

bool GpsParser::validateNmea(const String& line, uint8_t& checksumOut) const {
  if (!line.startsWith("$")) {
    return false;
  }

  const int asteriskIndex = line.indexOf('*');
  if (asteriskIndex <= 1 || asteriskIndex + 2 >= line.length()) {
    return false;
  }

  uint8_t calculated = 0;
  for (int i = 1; i < asteriskIndex; ++i) {
    calculated ^= static_cast<uint8_t>(line[i]);
  }

  const uint8_t expected = hexPairToByte(line[asteriskIndex + 1], line[asteriskIndex + 2]);
  checksumOut = calculated;
  return calculated == expected;
}

bool GpsParser::splitGngllBody(const String& body, GngllData& outData) const {
  char buffer[kLineBufferSize] = {};
  body.toCharArray(buffer, sizeof(buffer));

  char* fields[8] = {};
  size_t count = 0;
  char* context = nullptr;
  char* token = strtok_r(buffer, ",", &context);
  while (token != nullptr && count < 8) {
    fields[count++] = token;
    token = strtok_r(nullptr, ",", &context);
  }

  if (count < 8) {
    return false;
  }

  if (strcmp(fields[0], "GNGLL") != 0) {
    return false;
  }

  const String latitude = fields[1];
  const String latitudeHemisphere = fields[2];
  const String longitude = fields[3];
  const String longitudeHemisphere = fields[4];
  const String utcField = fields[5];
  const String status = fields[6];
  const String mode = fields[7];

  if (latitude.length() == 0 || longitude.length() == 0 || utcField.length() == 0 ||
      latitudeHemisphere.length() != 1 || longitudeHemisphere.length() != 1 ||
      status.length() != 1 || mode.length() != 1) {
    return false;
  }

  if (status[0] != 'A') {
    return false;
  }

  outData.latitude = latitude;
  outData.latitudeHemisphere = latitudeHemisphere[0];
  outData.longitude = longitude;
  outData.longitudeHemisphere = longitudeHemisphere[0];
  outData.utcTimeHhmmss = normalizeUtc(utcField);
  outData.status = status[0];
  outData.mode = mode[0];
  return outData.utcTimeHhmmss.length() != 0;
}

String GpsParser::normalizeUtc(const String& utcField) const {
  const int dotIndex = utcField.indexOf('.');
  const String wholeSeconds = dotIndex >= 0 ? utcField.substring(0, dotIndex) : utcField;
  if (wholeSeconds.length() < 6) {
    return String();
  }
  return wholeSeconds.substring(0, 6);
}

}  // namespace locator
