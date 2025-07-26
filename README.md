
# ESPHome ESP-NOW PubSub External Component

This component enables MQTT-like pub/sub messaging over ESP-NOW for ESP32 devices in ESPHome. It is designed for broadcast communication, allowing devices to subscribe to topics and receive messages with a topic/payload structure.

## Supported Platforms and Modes

- **Frameworks:** Works with both Arduino and ESP-IDF.
- **Network Modes:**
  - WiFi only
  - Ethernet only (LAN8720, etc.)
  - Standalone (no WiFi, no Ethernet)
- **ESP-NOW:** Always uses broadcast (no peer MACs, no encryption).

### ESP-IDF & Ethernet Note

When using ESP-IDF with Ethernet, ESPHome disables the WiFi driver by default. However, ESP-NOW requires the WiFi radio/driver even if only Ethernet is used for networking. This component automatically enables the WiFi driver in ESP-IDF builds by injecting the necessary Kconfig options:

```
CONFIG_ESP_WIFI_ENABLED=y
CONFIG_SW_COEXIST_ENABLE=y
```

No manual changes are needed; this is handled in the component's codegen.


## Features

- Robust ESP-NOW pub/sub messaging for ESP32
- Works with WiFi, Ethernet, or standalone (no network stack)
- Automatic WiFi driver enablement for ESP-NOW under ESP-IDF/Ethernet
- Broadcast-based (no peer MAC configuration required)
- Topic-based subscription and message dispatch
- MQTT-style wildcard topic matching (`+` and `#`)
- YAML integration for publish actions, on_message triggers, and status sensors
- Diagnostic logging via ESPHome logger
- Exposes the following sensors to ESPHome/Home Assistant:
  - Numeric sensor: Last received RSSI (signal strength)
  - Text sensor: Error description or current status
  - Numeric sensor: Count of sent messages
  - Numeric sensor: Count of received messages


## Usage Example

```yaml
external_components:

  - source: github://your/repo/components/espnow_pubsub


espnow_pubsub:
  id: my_pubsub
  channel: 6
  on_message:
    - topic: "test/topic"
      then:
        - logger.log: "Received test/topic!"

sensor:
  - platform: espnow_pubsub
    rssi:
      name: "ESP-NOW RSSI"
    sent_count:
      name: "ESP-NOW Sent Count"
    received_count:
      name: "ESP-NOW Received Count"
    id: my_pubsub

text_sensor:
  - platform: espnow_pubsub
    status_text:
      name: "ESP-NOW Status"
    id: my_pubsub


# To publish a message from an automation:
- espnow_pubsub.publish:
    id: my_pubsub
    topic: "test/topic"
    payload: "hello world"

## Example: Ethernet with ESP-IDF

```yaml
esp32:
  board: wt32-eth01
  framework:
    type: esp-idf

ethernet:
  type: LAN8720
  mdc_pin: GPIO23
  mdio_pin: GPIO18
  clk_mode: GPIO0_IN
  phy_addr: 1
  power_pin: GPIO16

external_components:
  - source:
      type: local
      path: components
    components:
      - espnow_pubsub

espnow_pubsub:
  id: my_pubsub
  channel: 1
  on_message:
    - topic: "test/topic"
      then:
        - logger.log:
            format: "Received test/topic: %s"
            args: ['payload.c_str()']
```

## Logging

- Publishing a message logs the topic and payload at info level.
- Receiving a message logs the topic, payload, and subscription status (subscribed/not subscribed) at info level.
- Diagnostic logs (WiFi/ESP-NOW state, events, etc.) are at verbose/debug level.


## Implementation Notes

- All ESP-NOW communication is broadcast; no explicit peer registration is required.
- Message queue ensures safe handling outside interrupt context. If the queue is full (16 messages), the oldest message is dropped and a warning is logged.
- Loop disables itself when no messages are pending for efficiency.
- Subscriptions support MQTT-style wildcards: `+` (single-level) and `#` (multi-level, must be last token).
- All communication is unencrypted (ESP-NOW encryption is not supported for broadcast).
- The following sensors are available:
  - `rssi_sensor`: Last received ESP-NOW RSSI (dBm)
  - `status_text_sensor`: Current error or status description
  - `sent_count_sensor`: Number of messages sent since boot
  - `received_count_sensor`: Number of messages received since boot

## File Structure

- `espnow_pubsub.cpp`/`.h`: C++ implementation and header
- `__init__.py`: Python integration for ESPHome YAML
- `README.md`: This documentation
- `component-tester.yaml`: Example configuration



## Current Status

- Fully supports ESP32 with ESPHome under both Arduino and ESP-IDF
- Works with WiFi, Ethernet, or standalone (no network stack)
- ESP-NOW send/receive is robust in all supported modes
- MQTT-style wildcard topic matching is implemented for subscriptions (use `+` and `#` in YAML `on_message` topics)
- Message queue is bounded (16 messages); if full, the oldest message is dropped and a warning is logged
- ESP-NOW error/status is logged and exposed as a text sensor
- README and YAML examples are kept up to date with implementation
- Peer-to-peer (unicast) encrypted ESP-NOW is not supported (broadcast only)

## Changelog

- 2025-07-25: Robust support for ESP-NOW with WiFi, Ethernet, and standalone (no network stack) under both Arduino and ESP-IDF; automatic WiFi driver enablement for ESP-IDF/Ethernet; improved documentation and YAML examples; ESP-NOW error/status exposed as text sensor; sensor and logging improvements
- 2025-07-22: MQTT-style wildcard topic matching implemented; documentation and implementation synchronized
- 2025-07-20: Logging and message handling updated for clarity and safety
- 2025-07-20: README updated to match implementation and add TODOs
