# ESP32-S3 4G GPS Locator

## Overview

This project runs on `ESP32-S3` and connects to one GPS module and one 4G modem over UART.
The device reads GPS NMEA data, extracts valid `$GNGLL` location data, and reports compact
location messages to the server through the modem's built-in MQTT AT commands.

The current project is a `PlatformIO + Arduino` firmware with:

- `platform = espressif32`
- `board = esp32-s3-devkitc-1`
- `framework = arduino`

## Runtime Architecture

The code is intentionally split into clear layers:

- `src/gps_parser.*`
  Parses NMEA input, validates checksum, and extracts compact location payloads.
- `src/modem_at.*`
  Owns all modem AT commands and wraps them as semantic methods.
- `src/app_controller.*`
  Drives initialization, health checking, GPS state tracking, and MQTT publishing.
- `include/config.h`
  Central place for device pins, timing, MQTT defaults, log level, and location policy.

`AppController` should not construct raw AT strings. It only calls semantic methods from
`ModemAtClient`, which keeps modem behavior isolated and easier to maintain.

## Initialization Stages

Initialization is split into two visible stages in logs and in the internal state machine.

### 1. Device Initialization

This stage checks the modem itself and reads static device information.

Expected startup URCs:

- `RDY`
- `SIM_SUCCESS`
- `NETWORK_ACTIVATE_SUCCESS`

Startup is considered ready only after all three are observed.

During device initialization, the firmware performs:

- `AT`
  Health check. Success is `OK`.
- `AT+GSN`
  Query IMEI.
- `AT+GMR`
  Query modem firmware version.
- `AT+CREG?`
  Query network registration state.
- `AT+QCCID`
  Query ICCID.
- `AT+SINGLESIM?`
  Query SIM slot.

### 2. MQTT Connection Initialization

This stage prepares the MQTT session and only succeeds when the end-to-end path is usable.

Sequence:

1. Register MQTT client information.
2. Configure MQTT broker information.
3. Start MQTT session.
4. Verify MQTT status.
5. Publish one startup test message: `test+<device_name>`.

Only after all five steps succeed is initialization considered complete.

Special handling:

- `AT+QMTCONNCFG` returning `ERROR=202` is treated as reusable existing broker config.
- MQTT status must indicate connected before runtime reporting starts.

## GPS State Model

GPS startup and GPS location are treated as different concepts.

Device-side GPS states:

- `not_started`
  No GPS serial data has been seen yet.
- `offline`
  GPS had produced serial data before, but no new UART data has been received within the
  configured offline timeout.
- `searching`
  GPS serial data is flowing, but no valid fix is available yet.
- `located`
  A valid fix is available and not stale.
- `unable`
  GPS data is flowing, but no valid fix was obtained within the configured timeout.

Rules:

- "GPS started successfully" means serial data is being received.
- "GPS located successfully" means there is a valid recent fix.
- If GPS cannot locate within the configured timeout, the device still keeps running and
  reports zero payloads instead of blocking the whole application.

## Reporting Protocol

To reduce traffic, power use, and backend storage pressure, location publishing is compact and
state-aware rather than sending the same full point every cycle.

The location topic payload uses three packet types:

- `F:<full_payload>`
  Full location payload. Used for first valid fix, movement, or periodic resync.
- `S:<still_seconds>`
  Still-state keepalive. Used when the device remains near the same place long enough.
- `Z:0`
  No-fix keepalive. Used when GPS is not currently located.

Examples:

```text
F:3956.20359N,11622.44467E,090353AA*4C
S:600
Z:0
```

### Full Packet `F:`

Sent when any of the following is true:

- The device gets its first valid fix.
- The device moved beyond the configured movement threshold.
- The last published state was `Z:0` and GPS has recovered.
- The periodic full resync interval has been reached.

### Still Packet `S:`

Sent when all of the following are true:

- GPS is located.
- The current position remains within the configured still-distance threshold.
- The still-confirm time has been reached.
- The still keepalive interval is due.

`S:<still_seconds>` means the backend should continue the previous full-position segment and
extend the stationary duration by the provided number of seconds.

### No-Fix Packet `Z:0`

Sent when GPS is not currently located and the no-fix keepalive interval is due.

Backend interpretation:

- `Z:0` means "device is alive, MQTT is alive, but location is unavailable now".
- Backend should not create a fake location point from `Z:0`.
- If a later `F:` arrives, backend should start a new valid location segment from that packet.

## Backend Aggregation Guidance

The backend or management platform should not store every publish as an independent raw point.
Recommended behavior:

- For `F:...`
  Start a new location segment or update the current moving point.
- For `S:...`
  Extend the previous location segment's stationary duration instead of inserting another full point.
- For `Z:0`
  Record device alive / no-fix state, but do not insert a normal coordinate record.

This approach reduces:

- MQTT traffic
- modem active time
- battery consumption
- database write count
- repeated duplicate coordinates

## Log Levels

Log output is controlled by `include/config.h`.

- `AppLogLevel::kApp`
  Production mode. Only prints initialization success/failure and periodic `[STATUS]`.
- `AppLogLevel::kDebug`
  Prints detailed AT, URC, GPS, and step-by-step initialization logs.

This is intended to reduce UART log overhead in deployed devices and improve battery life.

## Configurable Parameters

All current policy thresholds are defined in `include/config.h` so they can be adjusted without
touching runtime logic.

Important parameters:

- `kGpsOfflineAfterMs`
  How long GPS UART can stay silent before the state becomes `offline`.
- `kGpsUnableToLocateAfterMs`
  Maximum time to wait before GPS enters `unable`.
- `kGpsStaleAfterMs`
  How long a previous fix is considered fresh.
- `kModemHealthCheckIntervalMs`
  Interval for runtime modem health checks using `AT`.
- `kMqttPublishIntervalMs`
  Base report scheduling interval.
- `kLocationMovementThresholdMeters`
  Movement threshold that triggers a new `F:` packet.
- `kLocationStillDistanceThresholdMeters`
  Distance limit for considering the device still stationary.
- `kLocationStillConfirmMs`
  How long the position must stay still before `S:` is allowed.
- `kLocationStillKeepaliveMs`
  How often to repeat `S:` while still stationary.
- `kLocationNoFixKeepaliveMs`
  How often to repeat `Z:0` while no fix is available.
- `kLocationFullResyncMs`
  Maximum interval before forcing another full `F:` packet.

Protocol constants are also centralized:

- `kReportFullPrefix`
- `kReportStillPrefix`
- `kReportNoFixPrefix`
- `kLocationZeroPayload`

## Future Remote Configuration

The current firmware uses compile-time defaults from `include/config.h`, but the design should
support MQTT-delivered runtime overrides later.

Reserved direction:

- status reporting topic: `kMqttStatusTopic`
- config downlink topic: `kMqttConfigTopic`
- switch: `kEnableRemoteConfigOverride`

Expected future remote-config scope:

- publish interval
- no-fix timeout
- still-distance threshold
- still-confirm duration
- still keepalive interval
- no-fix keepalive interval
- full resync interval
- movement threshold
- log level

Recommended principle:

- `config.h` holds factory defaults.
- MQTT management platform can deliver overrides.
- device should acknowledge applied values and report effective configuration back.

## Current Runtime Polling Policy

To reduce unnecessary modem activity, runtime polling is intentionally minimal.

Runtime loop should only do:

- periodic modem health check through `AT`
- periodic MQTT publish according to the compact reporting strategy

The following items are treated as init-time or on-demand information and should not be polled
continuously during normal running:

- IMEI
- modem firmware version
- ICCID
- SIM slot
- network registration details
- MQTT diagnostic details

`CSQ` should be queried only when there is a specific request for signal quality.

## Build Version

`include/config.h` exposes:

- `kFirmwareVersion = __DATE__ " " __TIME__`

This is printed at boot and included in status output so field logs can be matched to the exact
binary that is currently flashed.
