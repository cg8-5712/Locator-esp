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
static constexpr FirmwareMode kFirmwareMode = FirmwareMode::kApp;
static constexpr MonitorTarget kMonitorTarget = MonitorTarget::kModemOnly;

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
static constexpr uint32_t kGpsStaleAfterMs = 15000;

static constexpr const char* kDeviceName = "locator-esp32s3-001";
static constexpr const char* kMqttClientId = kDeviceName;
static constexpr const char* kMqttUsername = "MQTT1";
static constexpr const char* kMqttPassword = "123456";
static constexpr const char* kMqttBroker = "broker.emqx.io";
static constexpr uint16_t kMqttPort = 1883;
static constexpr bool kMqttAutoReconnect = true;
static constexpr uint8_t kMqttCleanSession = 1;
static constexpr uint16_t kMqttKeepAliveSeconds = 30;
static constexpr uint32_t kMqttPublishIntervalMs = 5000;
static constexpr const char* kMqttLocationTopic = "locator/locator-esp32s3-001/location";
static constexpr const char* kMqttTestTopic = "locator/locator-esp32s3-001/test";

}  // namespace config
}  // namespace locator
