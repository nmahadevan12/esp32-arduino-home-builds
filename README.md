# Past ESP32 / Arduino home builds

Personal ESP32 and Arduino sketches from a small home stack: wireless door sensors, Discord hubs, Wi‑Fi blinds, and lamp automation.

Each folder is one project from the [portfolio writeup](https://nmahadevan12.github.io/projects/esp32-home-builds/).

| Folder | What it is |
| --- | --- |
| [`espnow-door-sensor`](./espnow-door-sensor) | Door system — `node/` + `hub/` |
| [`esp-receiver-autowifi`](./esp-receiver-autowifi) | WiFiManager web hub |
| [`esp-receiver-autowifi-discord`](./esp-receiver-autowifi-discord) | Same hub family with Discord webhooks |
| [`security-system`](./security-system) | Wi‑Fi / web security logger |
| [`home-automation`](./home-automation) | Hub + Wi‑Fi blinds + lamps (`hub/` · `blinds/` · `lamps/`) |
| [`photo-blinds`](./photo-blinds) | Standalone photoresistor blinds (not hub-controlled) |

## Setup notes

Wi‑Fi SSIDs, passwords, and login hashes in these sketches are **placeholders**. Replace them before flashing. Webhooks and API tokens are loaded from SPIFFS / the web UI where applicable — nothing secret is committed here.
