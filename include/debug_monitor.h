#pragma once

#include <Arduino.h>

namespace locator {

class DebugMonitorApp {
 public:
  DebugMonitorApp(Stream& debug, Stream& gpsStream, Stream& modemStream);

  void begin();
  void poll();

 private:
  static constexpr size_t kLineBufferSize = 192;

  void pollUsbInput();
  void pollGps();
  void pollModem();
  void runAutoTests();
  void processUsbLine();
  void processGpsLine(const char* line, size_t length);
  void processModemLine(const char* line, size_t length);

  static bool appendChar(
      char c,
      char* buffer,
      size_t capacity,
      size_t& length,
      bool& droppingLine);
  static String escapeBytes(const char* data, size_t length);
  static String hexBytes(const char* data, size_t length);
  static bool isPrintableAsciiLine(const char* data, size_t length);
  static bool validateNmea(const String& line, uint8_t& calculated, uint8_t& expected);
  static void printEscapedLine(
      Stream& debug,
      const __FlashStringHelper* prefix,
      const char* data,
      size_t length);

  Stream& debug_;
  Stream& gpsStream_;
  Stream& modemStream_;

  char usbLineBuffer_[kLineBufferSize] = {};
  size_t usbLineLength_ = 0;
  bool droppingUsbLine_ = false;

  char gpsLineBuffer_[kLineBufferSize] = {};
  size_t gpsLineLength_ = 0;
  bool droppingGpsLine_ = false;

  char modemLineBuffer_[kLineBufferSize] = {};
  size_t modemLineLength_ = 0;
  bool droppingModemLine_ = false;

  uint32_t lastHeartbeatAtMs_ = 0;
  uint32_t startedAtMs_ = 0;
  uint32_t lastAtProbeAtMs_ = 0;
  bool gpsPassed_ = false;
  bool modemPassed_ = false;
  bool waitingForAtOk_ = false;
};

}  // namespace locator
