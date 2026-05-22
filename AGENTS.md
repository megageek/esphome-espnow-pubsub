# ESPHome ESP-NOW PubSub — Agent Guide

## What this is

An ESPHome external component (`components/espnow_pubsub/`) providing MQTT-like pub/sub over ESP-NOW broadcast for ESP32. Always broadcast, no peer MACs, no encryption.

## Key files

| Path | Purpose |
|------|---------|
| `components/espnow_pubsub/__init__.py` | YAML schema, codegen, action registration |
| `components/espnow_pubsub/espnow_pubsub.cpp` | Core C++: send/receive, dedup, topic matching |
| `components/espnow_pubsub/espnow_pubsub.h` | Header with `EspNowPubSub` class |
| `components/espnow_pubsub/sensor.py` | Sensor platform (rssi, sent_count, received_count) |
| `components/espnow_pubsub/text_sensor.py` | Text sensor platform (status_text) |
| `tests/` | YAML configs for `esphome config`/`compile` validation |

## Validation (no test runner)

There are no unit tests — only YAML config validation via ESPHome:

```bash
# Validate config
esphome config tests/test_standalone.yaml
esphome config tests/test_wifi.yaml
esphome config tests/test_ethernet.yaml

# Compile firmware
esphome compile tests/test_standalone.yaml

# Upload to device
esphome upload tests/test_standalone.yaml
```

## Architecture notes

- **Dependency chain:** `espnow_pubsub` → `espnow` (native ESPHome component). The YAML must also have an `espnow:` block.
- **Network modes:** standalone (`wifi: ap: {}`), WiFi (full station), Ethernet (requires ESP-IDF framework + WiFi drivers). All three use the same component.
- **Message format:** `[seq:uint32][topic\0][payload]` — topic and payload are NUL-delimited.
- **Deduplication:** Sequence number tracked per source MAC; duplicates are dropped in `on_broadcasted()`.
- **Queue:** Max 16 messages in interrupt context; oldest dropped on overflow. `loop()` drains the queue then disables itself.
- **`send_times`** controls retransmission count for reliability (default 1, max 10).
- **Wildcard matching:** Full MQTT-style (`+` for single-level, `#` for multi-level, must be last token).
- **`on_message` automation** receives `topic` (string), `payload` (string), `sequence` (uint32) as trigger args.

## Gotchas

- `tests/test_wifi.yaml` and `tests/test_wifi_gateway.yaml` require `tests/secrets.yaml` (copied from `tests/secrets.yaml.example`) with WiFi credentials. `secrets.yaml` is gitignored.
- Test YAMLs reference components via an **absolute path** (`source: /home/supergeek/.../components`). Change this for your setup or use `github://megageek/esphome-espnow-pubsub/components/espnow_pubsub`.
- `sensor.py`/`text_sensor.py` use `DEPENDENCIES = ["espnow_pubsub"]`. This was previously misspelled `DEPENDANCIES` — if you see a bare sensor config without the parent component initializing, check that.
- All test configs use **ESP-NOW channel 6**. For real deployments ensure all devices share the same channel.
- `loop()` uses `disable_loop()` when idle — it re-enables via `enable_loop_soon_any_context()` from the receive handler. Don't forget to call `enable_loop_soon_any_context()` if adding new async work.
