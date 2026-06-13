#pragma once

#include <Arduino.h>

namespace locator {
namespace config {

static constexpr uint32_t kDebugBaudRate = 115200;
static constexpr uint32_t kGpsBaudRate = 9600;
static constexpr uint32_t kModemBaudRate = 115200;

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
