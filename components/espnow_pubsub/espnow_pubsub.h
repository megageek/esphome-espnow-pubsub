// MIT License
// Copyright (c) 2025 Mark Johnson
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once
#include "esphome/core/defines.h"
#include "esphome/core/component.h"
#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#ifdef USE_SENSOR
#include "esphome/components/sensor/sensor.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif
#include <vector>
#include <functional>
#include <string>
#include <utility>
#include <unordered_map>
#include <esp_now.h>

namespace esphome {
namespace espnow_pubsub {

// Helper: MQTT topic match with wildcards
// Supports + (single-level) and # (multi-level) wildcards
bool mqtt_topic_matches(const std::string &sub, const std::string &topic);

class OnMessageTrigger; // Forward declaration

class EspNowPubSub : public Component {
 public:
  using MessageCallback = std::function<void(const std::string &topic, const std::string &payload)>;

  EspNowPubSub();
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  void setup() override;
  void loop() override;
  void dump_config() override;

  void set_channel(int channel) { channel_ = channel; }

  // Only compile-time subscriptions via add_subscription
  void add_subscription(const std::string &topic, OnMessageTrigger *trigger);

  void publish(const std::string &topic, const std::string &payload);
  void receive_message(const std::string &topic, const std::string &payload);
  
  void on_espnow_receive(const esp_now_recv_info *recv_info, const uint8_t *mac_addr, const uint8_t *data, int len);

  // Re-initialize ESP-NOW after WiFi events
  void reinit_espnow();
  // Standalone ESP-NOW initialization (no WiFi component)
  void init_espnow_standalone();
  // Common ESP-NOW initialization
  void init_espnow_common();
  // ESP-NOW initialization after WiFi connects and channel is valid
  void init_espnow_after_wifi(uint8_t wifi_channel);

  void set_send_times(int send_times) { send_times_ = send_times; }
  
 // Sensor setters
#ifdef USE_SENSOR
  void set_rssi_sensor(esphome::sensor::Sensor *sensor) { rssi_sensor_ = sensor; }
  void set_sent_count_sensor(esphome::sensor::Sensor *sensor) { sent_count_sensor_ = sensor; }
  void set_received_count_sensor(esphome::sensor::Sensor *sensor) { received_count_sensor_ = sensor; }
#endif
#ifdef USE_TEXT_SENSOR
  void set_status_text_sensor(esphome::text_sensor::TextSensor *sensor) { status_text_sensor_ = sensor; }
#endif

 protected:
  int channel_{1};
  struct Subscription {
    std::string topic;
    MessageCallback callback;
  };
  std::vector<Subscription> subscriptions_;
  // Track WiFi/ESP-NOW channel compatibility
  bool wifi_channel_compatible_ = true;
  std::string mac_address_;
  bool espnow_init_ok_ = false;
  int espnow_init_error_code_ = 0;
  int wifi_component_channel_ = -1;
  // Track if any subscriptions are present
  bool has_subscriptions_ = false;

 private:
  void update_mac_address();

  // Sensor pointers
  #ifdef USE_SENSOR
  esphome::sensor::Sensor *rssi_sensor_{nullptr};
  esphome::sensor::Sensor *sent_count_sensor_{nullptr};
  esphome::sensor::Sensor *received_count_sensor_{nullptr};
  #endif
  #ifdef USE_TEXT_SENSOR
  esphome::text_sensor::TextSensor *status_text_sensor_{nullptr};
  #endif
  // Sensor state
  int last_rssi_ = 0;
  
  std::string last_status_;
  uint32_t sent_count_ = 0;
  uint32_t received_count_ = 0;
  // Structure to hold queued received messages
  struct QueuedMessage {
    std::string topic;
    std::string payload;
  };
  std::vector<QueuedMessage> message_queue_;
  // Maximum number of messages allowed in the queue (overflow handling)
  static constexpr size_t MAX_QUEUE_SIZE = 16;

  int send_times_{1};
  // Track last seen sequence number per MAC to filter duplicates while
  // permitting counter resets after a reboot or wrap-around.
  std::unordered_map<std::string, uint32_t> last_sequence_by_mac_;
};

// OnMessageTrigger: Trigger for incoming messages on a topic
class OnMessageTrigger : public Trigger<std::string, std::string> {
 public:
  OnMessageTrigger(EspNowPubSub *parent, const std::string &topic);
};

// EspnowPubSubPublishAction: Action to publish a message to a topic
template<typename... Ts>
class EspnowPubSubPublishAction : public Action<Ts...> {
 public:
  EspnowPubSubPublishAction(EspNowPubSub *parent);
  void set_topic(TemplatableValue<std::string, Ts...> topic);
  void set_payload(TemplatableValue<std::string, Ts...> payload);
  void play(Ts... x) override;

 protected:
  EspNowPubSub *parent_ = nullptr;
  TemplatableValue<std::string, Ts...>  topic_;
  TemplatableValue<std::string, Ts...> payload_;
};

}  // namespace espnow_pubsub
}  // namespace esphome

// Declare the global singleton pointer for ESP-NOW receive callback
namespace esphome {
namespace espnow_pubsub {
  extern EspNowPubSub *global_espnow_pubsub_instance;
}
}
