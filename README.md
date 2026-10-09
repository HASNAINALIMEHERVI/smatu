# SMATU — ESP8266 notification companion prototype

A personal hardware/software project combining a NodeMCU ESP8266, OLED display, buzzer, alarms and an MQTT browser dashboard. It demonstrates embedded and browser integration; it is not a commissioned client website or a production secure IoT service.

## Build and use

Install PlatformIO, run `pio run` in this folder, and flash the configured `nodemcuv2` board only after confirming the wiring and pin definitions in `src/main.cpp`. Serve `index.html` over localhost or HTTPS for the browser dashboard. `commercial.html` redirects to the maintained dashboard.

## Audit changes

MQTT JSON and array indices are validated before handling commands. Commands require the configured demo PIN. Notification/history rendering uses text nodes, malformed local storage is tolerated, and unpairing removes only this app's keys. The PWA now references an included SVG icon and caches local assets without attempting to precache the entire internet.

## Limits

The MQTT broker is public and the device connection uses plaintext MQTT. A four-digit PIN is a demonstration control, not strong authentication; anyone observing traffic can obtain it. The local device HTTP configuration interface is intended for a trusted network. Production use requires a private broker, TLS and authenticated device/users plus hardened configuration endpoints. No deployed board or physical alarm operation has been tested in this audit. Browser control requires network access and the CDN MQTT library; an offline cached page cannot control the device.
