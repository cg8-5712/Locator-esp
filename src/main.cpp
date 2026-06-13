#include <Arduino.h>

#include "app_controller.h"
#include "config.h"

namespace {

HardwareSerial gpsSerial(1);
HardwareSerial modemSerial(2);

locator::AppController* gApp = nullptr;

}  // namespace

void setup() {
  Serial.begin(locator::config::kDebugBaudRate);
  delay(200);

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

  static locator::AppController app(Serial, gpsSerial, modemSerial);
  gApp = &app;

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

  gApp->begin();
}

void loop() {
  if (gApp != nullptr) {
    gApp->poll();
  }
}
