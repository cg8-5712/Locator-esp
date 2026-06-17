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

enum class AppLogLevel : uint8_t {
  kApp = 0,
  kDebug = 1,
};

static constexpr uint32_t kDebugBaudRate = 115200;
static constexpr uint32_t kGpsBaudRate = 9600;
static constexpr uint32_t kModemBaudRate = 115200;
static constexpr const char* kFirmwareVersion = __DATE__ " " __TIME__;
static constexpr AppLogLevel kAppLogLevel = AppLogLevel::kApp;

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
static constexpr uint32_t kGpsOfflineAfterMs = 10000;

// Runtime health / reporting cadence.
static constexpr uint32_t kGpsUnableToLocateAfterMs = 30000;
static constexpr uint32_t kModemHealthCheckIntervalMs = 30000;
static constexpr uint32_t kMqttPublishIntervalMs = 30000;

// Compact location reporting policy.
static constexpr uint32_t kLocationStillDistanceThresholdMeters = 30;
static constexpr uint32_t kLocationMovementThresholdMeters = 30;
static constexpr uint32_t kLocationStillConfirmMs = 300000;
static constexpr uint32_t kLocationStillKeepaliveMs = 900000;
static constexpr uint32_t kLocationNoFixKeepaliveMs = 900000;
static constexpr uint32_t kLocationFullResyncMs = 3600000;

// Compact payload markers used by the device and expected by the backend.
static constexpr const char* kReportFullPrefix = "F:";
static constexpr const char* kReportStillPrefix = "S:";
static constexpr const char* kReportNoFixPrefix = "Z:";
static constexpr const char* kLocationZeroPayload = "0,0,0";

// Future management platform support: compile-time defaults can later be
// overridden by MQTT-delivered runtime configuration.
static constexpr bool kEnableRemoteConfigOverride = false;

static constexpr const char* kDeviceName = "locator-esp32s3-001";
static constexpr const char* kMqttClientId = kDeviceName;
static constexpr const char* kMqttUsername = "CT12-5712";
static constexpr const char* kMqttPassword = "test123456";
static constexpr const char* kMqttBroker = "39.106.198.100";
static constexpr uint16_t kMqttPort = 1883;
static constexpr bool kMqttAutoReconnect = true;
static constexpr uint8_t kMqttCleanSession = 1;
static constexpr uint16_t kMqttKeepAliveSeconds = 30;
static constexpr const char* kMqttLocationTopic = "locator/locator-esp32s3-001/location";
static constexpr const char* kMqttTestTopic = "locator/locator-esp32s3-001/test";
static constexpr const char* kMqttStatusTopic = "locator/locator-esp32s3-001/status";
static constexpr const char* kMqttConfigTopic = "locator/locator-esp32s3-001/config";

}  // namespace config
}  // namespace locator
