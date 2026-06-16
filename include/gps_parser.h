#pragma once

#include <Arduino.h>

namespace locator {

struct GngllData {
  String rawSentence;
  String latitude;
  char latitudeHemisphere = '\0';
  String longitude;
  char longitudeHemisphere = '\0';
  String utcTimeHhmmss;
  char status = '\0';
  char mode = '\0';
  uint8_t checksum = 0;
  String compactPayload;
};

struct GpsStats {
  uint32_t totalLines = 0;
  uint32_t totalGngll = 0;
  uint32_t validFixes = 0;
  uint32_t checksumErrors = 0;
  uint32_t invalidFixes = 0;
  uint32_t overflowLines = 0;
};

class GpsParser {
 public:
  void begin(Stream& stream);
  void poll();

  bool hasNewFix() const;
  GngllData takeLatestFix();
  const GpsStats& stats() const;
  uint32_t lastValidFixAtMs() const;
  bool hasSeenAnyData() const;
  uint32_t firstDataAtMs() const;

 private:
  static constexpr size_t kLineBufferSize = 160;

  bool processLine(const char* line);
  bool parseGngllSentence(const String& line, GngllData& outData);
  bool validateNmea(const String& line, uint8_t& checksumOut) const;
  bool splitGngllBody(const String& body, GngllData& outData) const;
  String normalizeUtc(const String& utcField) const;

  Stream* stream_ = nullptr;
  char lineBuffer_[kLineBufferSize] = {};
  size_t lineLength_ = 0;
  bool droppingLine_ = false;

  bool hasNewFix_ = false;
  GngllData latestFix_;
  GpsStats stats_;
  uint32_t lastValidFixAtMs_ = 0;
  bool hasSeenAnyData_ = false;
  uint32_t firstDataAtMs_ = 0;
};

}  // namespace locator
