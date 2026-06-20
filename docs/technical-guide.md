# Technical Guide

## Purpose

This document describes the current firmware design for the `ESP32-S3 + GPS + 4G modem`
locator project. It is intended for maintainers, backend developers, and future management
platform work.

The guide covers:

- module boundaries
- initialization sequence
- GPS parsing behavior
- modem and MQTT behavior
- runtime state model
- compact reporting protocol
- configurable parameters
- future remote-configuration direction

## System Layout

Current code structure:

- `src/main.cpp`
  Firmware entry point and mode selection.
- `src/gps_parser.*`
  NMEA line collection, `$GNGLL` parsing, checksum validation, compact payload generation.
- `src/modem_at.*`
  AT command transport, response parsing, URC handling, modem status storage.
- `src/app_controller.*`
  Main application state machine, initialization flow, periodic health checking, report scheduling.
- `src/debug_monitor.*`
  UART diagnostics mode for bring-up and troubleshooting.
- `include/config.h`
  Pins, baud rates, timing, log level, MQTT defaults, and reporting policy thresholds.

Design rule:

- `AppController` drives behavior.
- `ModemAtClient` owns raw AT strings.
- GPS parsing and modem transport stay independent.

## GPS Data Handling

### Input

The firmware continuously reads GPS UART data line by line.

Only `$GNGLL` sentences are currently used for location reporting.

Example:

```text
$GNGLL,3956.20359,N,11622.44467,E,090353.000,A,A*4C
```

### Validation

The parser validates:

- sentence type is `$GNGLL`
- NMEA checksum is correct
- fields are present
- status field is `A`
- latitude, longitude, and UTC fields are non-empty

If validation fails:

- the fix is discarded
- parser statistics are updated
- the main loop continues without blocking

### Compact Payload Format

The current compact location payload is:

```text
3956.20359N,11622.44467E,090353AA*4C
```

Transformation rules:

- keep latitude numeric field and append hemisphere
- keep longitude numeric field and append hemisphere
- truncate UTC time to `hhmmss`
- append status
- append mode
- append original NMEA checksum

Important:

- this is a business payload, not a standard NMEA sentence
- `*4C` remains the checksum of the original `$GNGLL` sentence

## Modem Initialization and Status

### Startup Readiness

Modem startup readiness is based on these URCs:

- `RDY`
- `SIM_SUCCESS`
- `NETWORK_ACTIVATE_SUCCESS`

Only when all three are observed is modem startup considered ready.

### Device Initialization Sequence

Device initialization currently performs:

1. `AT`
2. `AT+GSN`
3. `AT+GMR`
4. `AT+CREG?`
5. `AT+QCCID`
6. `AT+SINGLESIM?`

Purpose:

- verify AT health
- read IMEI
- read modem firmware version
- read registration status
- read ICCID
- read SIM slot

These values are treated as init-time metadata and are not continuously polled during runtime.

### MQTT Initialization Sequence

After modem startup is ready, MQTT initialization performs:

1. register client credentials
2. configure broker information
3. start MQTT session
4. verify MQTT connection status
5. publish startup test message `test+<device_name>`

Initialization is complete only after all five steps succeed.

Special case:

- broker configuration returning `ERROR=202` is treated as reusable existing broker configuration

## Runtime State Model

### Application State Machine

Defined in `AppController`:

- `kBootDelay`
- `kInitModem`
- `kInitMqtt`
- `kRunning`
- `kRecovery`

Meaning:

- `kBootDelay`
  Wait before starting modem work.
- `kInitModem`
  Perform device initialization AT sequence.
- `kInitMqtt`
  Build MQTT end-to-end connectivity.
- `kRunning`
  Normal operation.
- `kRecovery`
  Backoff and restart initialization after failure.

### GPS State Model

The firmware separates GPS startup from GPS fix quality.

Current GPS states:

- `not_started`
  No GPS UART data has ever been seen.
- `offline`
  GPS UART had data before, but no new UART bytes arrived within `kGpsOfflineAfterMs`.
- `searching`
  GPS UART is active, but there is no valid current fix.
- `located`
  A valid fix exists and is not stale.
- `unable`
  GPS UART is active, but no valid fix was obtained within `kGpsUnableToLocateAfterMs`.

Implication:

- GPS can be "started" without being "located".
- GPS can be alive but still unable to locate.
- GPS can also become physically silent again and enter `offline`.

### Modem Status Dimensions

The firmware also tracks modem-related status independently:

- `startupReady`
- `healthCheckOk`
- `networkRegistered`
- `mqttConnected`

This is more useful than collapsing everything into one "online/offline" bit.

## Runtime Polling Policy

Runtime modem activity is intentionally minimized.

Normal running should only do:

- periodic modem health check using `AT`
- periodic MQTT publish according to the compact reporting policy

The following are not supposed to be polled continuously during normal running:

- IMEI
- modem firmware version
- ICCID
- SIM slot
- network registration detail queries
- MQTT diagnostic detail queries

Signal quality `CSQ` is intended to be queried only when explicitly needed.

## Compact Reporting Protocol

### Packet Types

The location topic uses three compact payload forms:

- `F:<full_payload>`
- `S:<still_seconds>`
- `Z:0`

Examples:

```text
F:3956.20359N,11622.44467E,090353AA*4C
S:600
Z:0
```

### `F:` Full Location

Sent when:

- first valid fix is available
- movement exceeds the configured threshold
- the previous report was `Z:0` and GPS recovers
- full resync interval is reached

### `S:` Still Keepalive

Sent when:

- GPS is located
- current position remains within the still-distance threshold
- still-confirm duration has elapsed
- still keepalive interval is due

Meaning:

- backend should extend the existing stationary segment
- backend should not store this as a separate full coordinate row

### `Z:0` No-Fix Keepalive

Sent when:

- GPS is not currently located
- no-fix keepalive interval is due

Meaning:

- the device is alive
- MQTT path is alive
- location is unavailable now

Backend should not convert `Z:0` into fake coordinates.

## Backend Integration Guidance

Recommended handling:

- `F:...`
  Create or refresh a real location segment.
- `S:...`
  Extend stationary duration for the previous real location segment.
- `Z:0`
  Record alive/no-fix state without creating a coordinate record.

Benefits:

- less traffic
- shorter modem active time
- lower power consumption
- fewer duplicate database records

## MQTT Management Interface

### Topics

In addition to the location topic, the firmware uses:

- `kMqttStatusTopic`
- `kMqttConfigTopic`
- `kMqttCmdTopic`

### Startup Publishing

After MQTT initialization succeeds, the device publishes:

1. startup test payload to `kMqttTestTopic`
2. one `status` payload
3. one `config` payload

### `status` Payload

Current format is compact JSON, for example:

```json
{"build":"Jun 20 2026 10:32:11","startup":1,"health":1,"gps":"located","net":1,"mqtt":1,"creg":1,"imei":"868478081658261","iccid":"89860412102570034386","fw":"+VERSION=CT12_V1.0.5"}
```

### `config` Payload

Current format is compact JSON, for example:

```json
{"pub_ms":30000,"gps_offline_ms":10000,"gps_unable_ms":30000,"move_m":30,"still_m":30,"still_confirm_ms":300000,"still_keepalive_ms":900000,"nofix_keepalive_ms":900000,"full_resync_ms":3600000,"health_ms":30000,"remote_cfg":1}
```

### Downlink Commands

The device subscribes to `kMqttCmdTopic` during MQTT initialization.

Supported commands:

```json
{"cmd":"get_status"}
{"cmd":"get_config"}
{"cmd":"set_config","pub_ms":60000,"move_m":50}
```

Behavior:

- `get_status`
  Schedules one publish to `kMqttStatusTopic`
- `get_config`
  Schedules one publish to `kMqttConfigTopic`
- `set_config`
  Applies supported runtime overrides, then republishes both `status` and `config`

### Current Remote-Config Scope

Supported override fields:

- `pub_ms`
- `gps_offline_ms`
- `gps_unable_ms`
- `move_m`
- `still_m`
- `still_confirm_ms`
- `still_keepalive_ms`
- `nofix_keepalive_ms`
- `full_resync_ms`
- `health_ms`
- `remote_cfg`

Current limitation:

- overrides are runtime-only
- there is no persistent storage yet
- command parsing is lightweight and assumes compact JSON without escaping complexity

## Logging Policy

Logging is controlled by `AppLogLevel` in `include/config.h`.

- `kApp`
  Production mode. Only initialization result and periodic `[STATUS]` output.
- `kDebug`
  Detailed AT commands, URCs, GPS parser statistics, and init-step traces.

This separation exists to reduce runtime overhead and battery impact in deployed units.

## Configurable Parameters

All key runtime thresholds are centralized in `include/config.h`.

Important groups:

### GPS

- `kGpsStaleAfterMs`
- `kGpsOfflineAfterMs`
- `kGpsUnableToLocateAfterMs`
- `kGpsHeartbeatIntervalMs`

### Modem and MQTT

- `kBootDelayMs`
- `kRecoveryDelayMs`
- `kModemHealthCheckIntervalMs`
- `kMqttPublishIntervalMs`
- `kMqttKeepAliveSeconds`

### Compact Reporting Policy

- `kLocationMovementThresholdMeters`
- `kLocationStillDistanceThresholdMeters`
- `kLocationStillConfirmMs`
- `kLocationStillKeepaliveMs`
- `kLocationNoFixKeepaliveMs`
- `kLocationFullResyncMs`

### Payload Markers

- `kReportFullPrefix`
- `kReportStillPrefix`
- `kReportNoFixPrefix`
- `kLocationZeroPayload`

## Future Remote Configuration

The code is being prepared for future MQTT-based management platform overrides.

Reserved items already exist:

- `kMqttStatusTopic`
- `kMqttConfigTopic`
- `kEnableRemoteConfigOverride`

Expected future remotely adjustable fields:

- publish interval
- GPS no-fix timeout
- GPS offline timeout
- movement threshold
- still-distance threshold
- still-confirm time
- still keepalive interval
- no-fix keepalive interval
- full resync interval
- log level

Recommended control model:

- `config.h` stores factory defaults
- platform sends override messages through MQTT
- device applies validated values
- device reports effective configuration back to the platform

## Suggested Next Steps

Likely future work items:

- add MQTT downlink command handling
- add remote configuration parsing and persistence policy
- add on-demand status and signal queries
- add backend message schema documentation if the platform format expands
- add tests for GPS parsing and state transitions
