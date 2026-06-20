# ESP32-S3 4G GPS Locator

Low-bandwidth GPS locator firmware for ESP32-S3 with UART GPS and UART 4G modem.

This project focuses on three goals:

- compact location reporting
- modem-controlled MQTT publishing
- minimal runtime polling for lower power use

## Highlights

- GPS location parsing is based on `$GNGLL`
- modem AT commands are wrapped in semantic functions
- initialization is split into device init and MQTT init
- runtime health check uses `AT` only
- location publishing uses compact `F:` / `S:` / `Z:` packets
- tunable thresholds live in `include/config.h`
- future MQTT-based remote configuration is reserved

## State Model

The firmware distinguishes these states:

- application state
- GPS state
- modem startup readiness
- modem health
- network registration
- MQTT connection

GPS states currently include:

- `not_started`
- `offline`
- `searching`
- `located`
- `unable`

## Reporting Protocol

The device publishes compact location payloads to reduce traffic and storage pressure.

- `F:<full_payload>` for first fix, movement, recovery, or periodic resync
- `S:<still_seconds>` for stationary keepalive
- `Z:0` for no-fix keepalive

Example:

```text
F:3956.20359N,11622.44467E,090353AA*4C
S:600
Z:0
```

Backend recommendation:

- treat `F:` as a full segment record
- treat `S:` as a stationary extension, not a new coordinate
- treat `Z:0` as alive/no-fix status, not a fake location point

## Configuration

Most policy values are centralized in `include/config.h`, including:

- GPS offline and unable-to-locate timeouts
- modem health interval
- MQTT publish interval
- movement and stillness thresholds
- resync and keepalive intervals
- log level

This is intended to make future MQTT-delivered runtime overrides easier.

## Build

PlatformIO project:

- board: `esp32-s3-devkitc-1`
- framework: `arduino`

Build command:

```bash
pio run
```

## Documentation

- [Technical Guide](docs/technical-guide.md)

## License

GPL-3.0-only. See [LICENSE](LICENSE).
