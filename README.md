# Past ESP32 / Arduino home builds

Personal ESP32 and Arduino sketches from a small home stack: wireless door sensors, Discord hubs, Wi‑Fi blinds, and lamp automation.

Each folder is one project from the [portfolio writeup](https://nmahadevan12.github.io/projects/esp32-home-builds/).

| Folder | What it is |
| --- | --- |
| [`espnow-door-sensor-node`](./espnow-door-sensor-node) | ESP-NOW reed-switch door node (deep sleep + heartbeat) |
| [`espnow-door-sensor-hub`](./espnow-door-sensor-hub) | ESP-NOW hub for door messages |
| [`esp-receiver-autowifi`](./esp-receiver-autowifi) | WiFiManager web hub |
| [`esp-receiver-autowifi-discord`](./esp-receiver-autowifi-discord) | Same hub family with Discord webhooks |
| [`security-system`](./security-system) | Wi‑Fi / web security logger |
| [`blinds-motor-controllers`](./blinds-motor-controllers) | Dual-blind H-bridge HTTP controllers |
| [`automation-hub`](./automation-hub) | LAN HTTP control plane for lights + blinds |
| [`lamps-and-sidelight`](./lamps-and-sidelight) | Wi‑Fi / HomeSpan lamps and timed sidelight |

## Setup notes

Wi‑Fi SSIDs, passwords, and login hashes in these sketches are **placeholders**. Replace them before flashing. Webhooks and API tokens are loaded from SPIFFS / the web UI where applicable — nothing secret is committed here.
