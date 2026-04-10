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
#include <vector>
#include <string>
#include <algorithm>
#include <cstring>
#include <esp_rom_sys.h>
#include "espnow_pubsub.h"
#include "esphome/core/log.h"
#include "esphome/components/espnow/espnow_component.h"
#include "esphome/components/espnow/espnow_packet.h"

namespace esphome {
namespace espnow_pubsub {

static const char *const TAG = "espnow_pubsub";

// Global callback context for send callbacks
// Using a simple atomic counter instead of std::function to avoid issues
static std::atomic<uint32_t> g_send_success_count{0};
static std::atomic<uint32_t> g_send_fail_count{0};

// MQTT-style topic matching with wildcards.
// This function checks if a given topic string matches a subscription pattern using MQTT wildcards:
//   - '#' matches all remaining topic levels (must be last token)
//   - '+' matches any single topic level
//
// Example matches:
//   sub = "foo/bar/#", topic = "foo/bar/baz/qux"   => true
//   sub = "foo/+/baz", topic = "foo/x/baz"          => true
//   sub = "foo/+/baz", topic = "foo/x/y/baz"        => false
//   sub = "foo/#", topic = "foo"                     => true
//   sub = "foo/#", topic = "foo/bar"                 => true
//   sub = "foo/bar", topic = "foo/bar"               => true
//   sub = "foo/bar", topic = "foo/bar/baz"           => false
//
// Called from receive_message() for every incoming message to determine if a subscription matches.
bool mqtt_topic_matches(const std::string &sub, const std::string &topic) {
  size_t sub_pos = 0, topic_pos = 0;
  // Iterate through both sub and topic, token by token (split by '/')
  while (sub_pos < sub.size() && topic_pos < topic.size()) {
    // Find next '/' in both sub and topic
    size_t sub_next = sub.find('/', sub_pos);
    size_t topic_next = topic.find('/', topic_pos);
    // Extract current token for sub and topic
    std::string sub_token = sub.substr(sub_pos, sub_next == std::string::npos ? sub.size() - sub_pos : sub_next - sub_pos);
    std::string topic_token = topic.substr(topic_pos, topic_next == std::string::npos ? topic.size() - topic_pos : topic_next - topic_pos);

    if (sub_token == "#") {
      // '#' matches all remaining topic levels, but must be last token in sub
      // If '#' is not the last token, it's not a valid match (strict MQTT semantics)
      return (sub_next == std::string::npos);
    } else if (sub_token == "+") {
      // '+' matches any single topic level, so continue to next token
    } else if (sub_token != topic_token) {
      // Tokens do not match and no wildcard, so not a match
      return false;
    }
    // Advance to next token in both sub and topic
    sub_pos = (sub_next == std::string::npos) ? sub.size() : sub_next + 1;
    topic_pos = (topic_next == std::string::npos) ? topic.size() : topic_next + 1;
  }
  // Allow trailing '#' in sub to match any remaining topic levels (including zero levels)
  if (sub_pos < sub.size() && sub.substr(sub_pos) == "#") return true;
  // Only match if both sub and topic are fully consumed
  return sub_pos == sub.size() && topic_pos == topic.size();
}

// Constructor
EspNowPubSub::EspNowPubSub() : Component() {
  ESP_LOGV(TAG, "Creating ESP-NOW PubSub component...");
}

// setup(): Register with native espnow component
void EspNowPubSub::setup() {
  ESP_LOGV(TAG, "Registering with native espnow component");

  // Enable auto peer addition for broadcast sending
  espnow::global_esp_now->set_auto_add_peer(true);

  // Register for receiving broadcasts
  espnow::global_esp_now->register_broadcasted_handler(this);

  last_status_ = "OK";
#ifdef USE_TEXT_SENSOR
  if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
}

// on_broadcasted(): Called by native espnow component when a broadcast is received
bool EspNowPubSub::on_broadcasted(const espnow::ESPNowRecvInfo &info,
                                  const uint8_t *data, uint8_t size) {
  ESP_LOGV(TAG, "[ON_BCAST] Received broadcast, size=%d", size);
  if (data == nullptr || size == 0) {
    ESP_LOGE(TAG, "[ON_BCAST] data is null or empty");
    last_status_ = "RX error: null/empty data";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    return false;
  }

  if (size <= sizeof(uint32_t)) {
    ESP_LOGE(TAG, "[ON_BCAST] Message too short: %d bytes", size);
    last_status_ = "RX error: message too short";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    return false;
  }

  // Parse seq + topic\0payload
  uint32_t seq = 0;
  memcpy(&seq, data, sizeof(uint32_t));
  const char *raw = reinterpret_cast<const char *>(data + sizeof(uint32_t));
  size_t remaining = size - sizeof(uint32_t);

  size_t topic_len = strnlen(raw, remaining);
  if (topic_len >= remaining - 1) {
    ESP_LOGE(TAG, "[ON_BCAST] Malformed message: topic_len=%zu, remaining=%zu", topic_len, remaining);
    last_status_ = "RX error: malformed message";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    return false;
  }

  std::string topic(raw, topic_len);
  std::string payload(raw + topic_len + 1, remaining - topic_len - 1);

  // Build MAC key for deduplication
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           info.src_addr[0], info.src_addr[1], info.src_addr[2],
           info.src_addr[3], info.src_addr[4], info.src_addr[5]);
  std::string mac_key(mac_str);

  // Deduplication check
  auto it = last_sequence_by_mac_.find(mac_key);
  if (it != last_sequence_by_mac_.end()) {
    uint32_t last_seq = it->second;
    if (seq == last_seq) {
      ESP_LOGV(TAG, "[ON_BCAST] Duplicate seq %u from %s ignored", seq, mac_str);
      return false;
    }
    if (seq < last_seq) {
      ESP_LOGV(TAG, "[ON_BCAST] Sequence reset from %u to %u for %s", last_seq, seq, mac_str);
    }
    it->second = seq;
  } else {
    last_sequence_by_mac_[mac_key] = seq;
  }

  ESP_LOGV(TAG, "[ON_BCAST] Queuing topic='%s', payload='%s', seq=%u", topic.c_str(), payload.c_str(), seq);

  // Queue overflow handling
  if (message_queue_.size() >= MAX_QUEUE_SIZE) {
    ESP_LOGW(TAG, "[ON_BCAST] Message queue full, dropping oldest");
    last_status_ = "RX warning: queue full";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    message_queue_.erase(message_queue_.begin());
  }
  message_queue_.push_back({topic, payload, seq});

  // Update RSSI and received count
#ifdef USE_SENSOR
  if (info.rx_ctrl) {
    last_rssi_ = info.rx_ctrl->rssi;
  }
#endif
  received_count_++;
  last_status_ = "OK";

  // Wake up the loop to process the message
  enable_loop_soon_any_context();
  return false;  // Don't stop propagation
}

// loop(): Process queued messages
void EspNowPubSub::loop() {
  static bool pending_sensor_update = false;

  // Process queued messages
  if (!message_queue_.empty()) {
    std::vector<QueuedMessage> local_queue;
    local_queue.swap(message_queue_);
    for (const auto &msg : local_queue) {
      ESP_LOGD(TAG, "[LOOP] Processing: topic='%s', payload='%s', seq=%u", msg.topic.c_str(), msg.payload.c_str(), msg.sequence);
      receive_message(msg.topic, msg.payload, msg.sequence);
    }
    pending_sensor_update = true;
    return;
  }

  // Publish sensor updates
  if (pending_sensor_update) {
#ifdef USE_SENSOR
    if (rssi_sensor_) rssi_sensor_->publish_state(last_rssi_);
    if (received_count_sensor_) received_count_sensor_->publish_state(received_count_);
#endif
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    pending_sensor_update = false;
    return;
  }

  // Idle - disable loop
  disable_loop();
}

// publish(): Send a broadcast message with send_times
// Uses native component's send queue
void EspNowPubSub::publish(const std::string &topic, const std::string &payload) {
  ESP_LOGI(TAG, "Publishing: topic='%s', payload='%s'", topic.c_str(), payload.c_str());

  // Build message: [seq:uint32][topic\0][payload]
  static uint32_t seq_counter = 0;
  uint32_t seq = seq_counter++;

  std::vector<uint8_t> msg;
  msg.resize(sizeof(uint32_t));
  memcpy(&msg[0], &seq, sizeof(uint32_t));
  msg.insert(msg.end(), topic.begin(), topic.end());
  msg.push_back('\0');
  msg.insert(msg.end(), payload.begin(), payload.end());

  // Queue sends with the native component (with callback to avoid crash)
  for (int i = 0; i < send_times_; i++) {
    esp_err_t err = espnow::global_esp_now->send(
        espnow::ESPNOW_BROADCAST_ADDR, msg,
        [](esp_err_t err) {});

    if (err == ESP_OK) {
      sent_count_++;
      ESP_LOGV(TAG, "Queued send (attempt %d)", i + 1);
    } else {
      ESP_LOGW(TAG, "Queue send failed on attempt %d: %d", i + 1, err);
    }

    // Small delay between queue attempts
    if (i < send_times_ - 1) {
      esp_rom_delay_us(1000);  // 1ms delay
    }
  }

#ifdef USE_SENSOR
  if (sent_count_sensor_) sent_count_sensor_->publish_state(sent_count_);
#endif

  last_status_ = "OK";
#ifdef USE_TEXT_SENSOR
  if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
}

// receive_message(): Match topic and trigger callbacks
void EspNowPubSub::receive_message(const std::string &topic, const std::string &payload, uint32_t sequence) {
  bool matched = false;
  for (const auto &sub : subscriptions_) {
    if (mqtt_topic_matches(sub.topic, topic)) {
      ESP_LOGI(TAG, "Matched topic '%s' with subscription '%s', payload='%s'",
               topic.c_str(), sub.topic.c_str(), payload.c_str());
      matched = true;
      sub.callback(topic, payload, sequence);
    }
  }
  if (!matched) {
    ESP_LOGD(TAG, "No subscription matched topic '%s'", topic.c_str());
  }
}

// dump_config(): Log configuration
void EspNowPubSub::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP-NOW PubSub:");
  ESP_LOGCONFIG(TAG, "  Repeat transmissions: %d", send_times_);
  ESP_LOGCONFIG(TAG, "  Subscriptions: %zu", subscriptions_.size());
  for (const auto &sub : subscriptions_) {
    ESP_LOGCONFIG(TAG, "    - %s", sub.topic.c_str());
  }

#ifdef USE_SENSOR
  if (rssi_sensor_) ESP_LOGCONFIG(TAG, "  Sensor: RSSI configured");
  if (sent_count_sensor_) ESP_LOGCONFIG(TAG, "  Sensor: Sent Count configured");
  if (received_count_sensor_) ESP_LOGCONFIG(TAG, "  Sensor: Received Count configured");
#endif
#ifdef USE_TEXT_SENSOR
  if (status_text_sensor_) ESP_LOGCONFIG(TAG, "  Text Sensor: Status configured");
#endif
}

// add_subscription(): Register a topic subscription
void EspNowPubSub::add_subscription(const std::string &topic, OnMessageTrigger *trigger) {
  subscriptions_.push_back({topic,
                            [trigger](const std::string &t, const std::string &p, uint32_t s) {
                              trigger->trigger(t, p, s);
                            }});
  ESP_LOGV(TAG, "Added subscription for topic: %s", topic.c_str());
}

// OnMessageTrigger
OnMessageTrigger::OnMessageTrigger(EspNowPubSub *parent, const std::string &topic) {}

// EspnowPubSubPublishAction
template<typename... Ts>
EspnowPubSubPublishAction<Ts...>::EspnowPubSubPublishAction(EspNowPubSub *parent) : parent_(parent) {}

template<typename... Ts>
void EspnowPubSubPublishAction<Ts...>::set_topic(TemplatableValue<std::string, Ts...> topic) {
  topic_ = std::move(topic);
}

template<typename... Ts>
void EspnowPubSubPublishAction<Ts...>::set_payload(TemplatableValue<std::string, Ts...> payload) {
  payload_ = std::move(payload);
}

template<typename... Ts>
void EspnowPubSubPublishAction<Ts...>::play(const Ts&... x) {
  auto topic = this->topic_.value(x...);
  auto payload = this->payload_.value(x...);
  auto *parent = parent_;
  if (parent != nullptr) {
    parent->publish(topic, payload);
  } else {
    ESP_LOGE(TAG, "Parent is null, cannot publish");
  }
}

// Explicit template instantiations
template class EspnowPubSubPublishAction<>;
template class EspnowPubSubPublishAction<float>;
template class EspnowPubSubPublishAction<std::string, std::string>;
template class EspnowPubSubPublishAction<int>;
template class EspnowPubSubPublishAction<bool>;
template class EspnowPubSubPublishAction<float, std::string>;
template class EspnowPubSubPublishAction<std::string, float>;
template class EspnowPubSubPublishAction<int, std::string>;
template class EspnowPubSubPublishAction<std::string, int>;

}  // namespace espnow_pubsub
}  // namespace esphome
