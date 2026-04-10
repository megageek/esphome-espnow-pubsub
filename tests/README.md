# ESPHome ESP-NOW PubSub Tests

This directory contains test configurations for validating the ESP-NOW PubSub component across all supported network modes.

## Network Modes

The component supports three network modes:

| Mode | Description | Configuration |
|------|-------------|---------------|
| **Standalone** | No WiFi/Ethernet, ESP-NOW runs independently | `wifi: ap: {}` |
| **WiFi** | Device connects to WiFi, ESP-NOW uses same channel | Full station config |
| **Ethernet** | Device uses Ethernet, ESP-NOW needs WiFi driver | `ethernet:` config |

## Single-Device Tests

| File | Mode | Purpose |
|------|------|---------|
| `test_standalone.yaml` | Standalone | Full feature test without network |
| `test_wifi.yaml` | WiFi | Full feature test with WiFi connection |
| `test_ethernet.yaml` | Ethernet | Full feature test with Ethernet (requires LAN8720 hardware) |

Each single-device test exercises:
- `espnow:` and `espnow_pubsub:` configuration
- `sensor:` platform (rssi, sent_count, received_count)
- `text_sensor:` platform (status_text)
- `button:` triggers for publish actions
- `script:` for periodic publishing
- `on_message:` triggers with MQTT wildcard matching

## Multi-Device Tests (Same Mode Communication)

| Files | Mode | Purpose |
|-------|------|---------|
| `test_wifi_gateway.yaml` + `test_wifi_node.yaml` | WiFi | Gateway receives, node publishes |
| `test_standalone_gateway.yaml` + `test_standalone_node.yaml` | Standalone | Gateway receives, node publishes |

Multi-device tests use the same ESP-NOW channel (6) and communicate via the `sensor/+/data` topic pattern.

## Usage

Validate a configuration:
```bash
esphome config tests/test_standalone.yaml
```

Compile a configuration:
```bash
esphome compile tests/test_standalone.yaml
```

Upload to device:
```bash
esphome upload tests/test_standalone.yaml
```

For WiFi configs, add secrets to `secrets.yaml`:
```yaml
wifi_ssid: "YourWiFiSSID"
wifi_password: "YourWiFiPassword"
```

## Testing Multi-Device Communication

1. Flash gateway config to one ESP32
2. Flash node config to another ESP32 (same channel)
3. Press the "Publish Sensor Data" button on the node
4. Check the gateway's serial logs for received message

## Real Device Configs

Pre-configured ESP-NOW pairs for M5Stack hardware:

| Files | Devices | Role | Description |
|-------|---------|------|-------------|
| `atom_lite_gateway.yaml` | M5Stack Atom Lite (ESP32) | Receiver | Subscribes to `sensor/+/data`, logs received messages |
| `atom_lite_s3_node.yaml` | M5Stack Atom Lite S3 (ESP32-S3) | Sender | Publishes `sensor/temp/data` when button pressed, LED feedback |

**Hardware:**
- **Atom Lite Gateway:** Standalone ESP-NOW receiver, RGB LED, serial logging
- **Atom Lite S3 Node:** Sends message on button press with green LED flash

**Usage:**
1. Flash `atom_lite_gateway.yaml` to M5Stack Atom Lite
2. Flash `atom_lite_s3_node.yaml` to M5Stack Atom Lite S3
3. Press button on Atom Lite S3
4. View received messages in serial logs on Atom Lite

## Notes

- All configs use ESP-NOW channel 6
- Ethernet config requires LAN8720 hardware
- WiFi configs require secrets configuration
- M5Stack Atom Lite S3 uses `esp32-s3-devkitc-1` board ID
