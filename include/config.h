#pragma once

#include <Arduino.h>

namespace locator {
namespace config {

enum class FirmwareMode : uint8_t {
  kApp = 0,
  kDebugMonitor = 1,
};

enum class MonitorTarget : uint8_t {
  kBoth = 0,
  kGpsOnly = 1,
  kModemOnly = 2,
};

static constexpr uint32_t kDebugBaudRate = 115200;
static constexpr uint32_t kGpsBaudRate = 9600;
static constexpr uint32_t kModemBaudRate = 115200;

// Switch between the regular app and a serial diagnostics firmware.
// kDebugMonitor is the recommended mode while debugging UART issues.
static constexpr FirmwareMode kFirmwareMode = static_cast<FirmwareMode>(1);
static constexpr MonitorTarget kMonitorTarget = static_cast<MonitorTarget>(2);

// Assumption for current wiring:
// 4G modem UART uses GPIO4/GPIO5, GPS UART uses GPIO13/GPIO14.
// The first pin in each pair is treated as ESP32 RX, the second as ESP32 TX.
static constexpr int kModemRxPin = 4;
static constexpr int kModemTxPin = 5;
static constexpr int kGpsRxPin = 13;
static constexpr int kGpsTxPin = 14;

static constexpr uint32_t kBootDelayMs = 10000;
static constexpr uint32_t kRecoveryDelayMs = 3000;
static constexpr uint32_t kStatusPrintIntervalMs = 10000;
static constexpr uint32_t kGpsHeartbeatIntervalMs = 5000;

}  // namespace config
}  // namespace locator
