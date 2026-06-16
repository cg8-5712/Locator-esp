#include <Arduino.h>

#include "app_controller.h"
#include "config.h"
#include "debug_monitor.h"

namespace {

HardwareSerial gpsSerial(1);
HardwareSerial modemSerial(2);

locator::AppController* gApp = nullptr;
locator::DebugMonitorApp* gDebugMonitor = nullptr;

}  // namespace

void setup() {
  Serial.begin(locator::config::kDebugBaudRate);
  delay(200);

  Serial.println();
  Serial.println(F("locator boot"));
  Serial.print(F("4G UART RX/TX = "));
  Serial.print(locator::config::kModemRxPin);
  Serial.print(F("/"));
  Serial.println(locator::config::kModemTxPin);
  Serial.print(F("GPS UART RX/TX = "));
  Serial.print(locator::config::kGpsRxPin);
  Serial.print(F("/"));
  Serial.println(locator::config::kGpsTxPin);
  Serial.println();
  Serial.println(F("Starting gps amd modem UARTs..."));

  gpsSerial.begin(
      locator::config::kGpsBaudRate,
      SERIAL_8N1,
      locator::config::kGpsRxPin,
      locator::config::kGpsTxPin);

  modemSerial.begin(
      locator::config::kModemBaudRate,
      SERIAL_8N1,
      locator::config::kModemRxPin,
      locator::config::kModemTxPin);


  if (locator::config::kFirmwareMode == locator::config::FirmwareMode::kDebugMonitor) {
    static locator::DebugMonitorApp debugMonitor(Serial, gpsSerial, modemSerial);
    gDebugMonitor = &debugMonitor;
    gDebugMonitor->begin();
  } else {
    static locator::AppController app(Serial, gpsSerial, modemSerial);
    gApp = &app;
    gApp->begin();
  }
}

void loop() {
  if (gDebugMonitor != nullptr) {
    gDebugMonitor->poll();
    return;
  }

  if (gApp != nullptr) {
    gApp->poll();
  }
}
