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
#include <esp_now.h>
#include <esp_event.h>
#include "esp_wifi.h"
#include "espnow_pubsub.h"
#include "esphome/core/log.h"
#include "esphome/core/defines.h"
#ifdef USE_WIFI
#include "esphome/components/wifi/wifi_component.h"
#endif

namespace esphome {
namespace espnow_pubsub {

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

static const char *const TAG = "espnow_pubsub";

// Forward declaration for event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);



// Constructor: Initializes the ESP-NOW PubSub component.
// Called by ESPHome when the component is created.
EspNowPubSub::EspNowPubSub() : Component(), espnow_init_ok_(false), espnow_init_error_code_(ESP_OK) {
  ESP_LOGV(TAG, "Creating ESP-NOW PubSub component...");
}


// setup(): Called by ESPHome during component initialization.
// Registers WiFi event handler and triggers ESP-NOW initialization.
void EspNowPubSub::setup() {

#ifdef USE_WIFI
  // Only register WiFi event handler if WiFi is present
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi != nullptr) {
    ESP_LOGV(TAG, "WiFi component detected, will initialize ESP-NOW after WiFi connects and channel is set.");
    // Register WiFi event handler for ESP-NOW re-init
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, this, &instance_any_id);
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) == ESP_OK && (mode == WIFI_MODE_STA || mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)) {
      wifi_ap_record_t ap_info;
      if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        uint8_t channel = ap_info.primary;
        ESP_LOGV(TAG, "[SETUP] WiFi already connected at setup, channel: %d. Calling init_espnow_after_wifi immediately.", (int)channel);
        this->init_espnow_after_wifi(channel);
        this->update_mac_address();
        return;
      } else {
        uint8_t channel = 0;
        esp_wifi_get_channel(&channel, nullptr);
        if (channel > 0) {
          ESP_LOGV(TAG, "[SETUP] WiFi AP mode active at setup, channel: %d. Calling init_espnow_after_wifi immediately.", (int)channel);
          this->init_espnow_after_wifi(channel);
          this->update_mac_address();
          return;
        }
      }
    }
    return;
  }
#endif
  // No WiFi component, do not register WiFi event handler
  ESP_LOGV(TAG, "No WiFi component detected, initializing ESP-NOW immediately.");
  this->init_espnow_standalone();
  this->update_mac_address();

// Add a private helper to update the MAC address string
}

void EspNowPubSub::update_mac_address() {
  uint8_t mac[6];
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    mac_address_ = mac_str;
  } else {
    mac_address_ = "(unavailable)";
  }
}

// Core ESP-NOW initialization logic.
// This function is called by both WiFi-managed and standalone initialization paths.
// It ensures the WiFi driver is started, sets the correct channel, manages power save,
// and registers the ESP-NOW broadcast peer and receive callback as needed.
//
// Steps:
// 1. Ensure WiFi is in a valid mode (STA/AP/APSTA). If not, set to STA mode.
// 2. Ensure WiFi is started (required for ESP-NOW to function).
// 3. Set the WiFi channel to the configured channel_ (must match across all devices).
// 4. Manage WiFi power save: disable if subscribing (for reliable RX), enable if standalone send-only.
// 5. Deinitialize and reinitialize ESP-NOW to ensure a clean state.
// 6. Register the broadcast peer (all-FF MAC, no encryption).
// 7. Register or unregister the receive callback depending on whether there are subscriptions.
// 8. Set status flags and log results.
void EspNowPubSub::init_espnow_common() {
  // Reset status flags
  espnow_init_ok_ = false;
  espnow_init_error_code_ = ESP_OK;
  esp_err_t err = ESP_OK;

  // ...existing code...

  // 1. Ensure WiFi is in a valid mode (STA/AP/APSTA). ESP-NOW requires WiFi driver to be running in a compatible mode.
  wifi_mode_t mode;
  esp_err_t mode_err = esp_wifi_get_mode(&mode);
  if (mode_err != ESP_OK || (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA)) {
    // If not, set to STA mode (safe default for ESP-NOW broadcast)
    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
      espnow_init_error_code_ = err;
      ESP_LOGE(TAG, "Failed to set WiFi mode for ESP-NOW: %d", err);
    }
  }

  // 2. Ensure WiFi is started (required for ESP-NOW to function)
  wifi_sta_list_t sta_list;
  bool wifi_started = (esp_wifi_ap_get_sta_list(&sta_list) == ESP_OK) || (esp_wifi_get_mac(WIFI_IF_STA, nullptr) == ESP_OK);
  if (!wifi_started) {
    err = esp_wifi_start();
    if (err != ESP_OK) {
      espnow_init_error_code_ = err;
      ESP_LOGE(TAG, "Failed to start WiFi for ESP-NOW: %d", err);
      return;
    }
  }

  // 3. Set the WiFi channel to the configured channel_ (must match across all devices for ESP-NOW broadcast to work)
  esp_err_t ch_err = esp_wifi_set_channel(channel_, WIFI_SECOND_CHAN_NONE);
  if (ch_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set WiFi channel to %d: %d", channel_, (int)ch_err);
  }

  // 4. Manage WiFi power save:
  //    - If this device subscribes to topics (i.e., expects to receive messages), disable power save for reliable RX.
  //    - If this device is standalone send-only (no subscriptions), enable max power save for efficiency.
  bool is_standalone = true;
#ifdef USE_WIFI
  auto *wifi = esphome::wifi::global_wifi_component;
  if (wifi != nullptr) is_standalone = false;
#endif
  if (has_subscriptions_) {
    err = esp_wifi_set_ps(WIFI_PS_NONE);
    if (err != ESP_OK) {
      espnow_init_error_code_ = err;
      ESP_LOGW(TAG, "Failed to disable power-save after WiFi start: %d", err);
    }
  } else if (is_standalone) {
    err = esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
    if (err != ESP_OK) {
      espnow_init_error_code_ = err;
      ESP_LOGW(TAG, "Failed to enable power-save in standalone send-only mode: %d", err);
    }
  }

  // 5. Deinitialize and reinitialize ESP-NOW to ensure a clean state
  esp_now_deinit();
  err = esp_now_init();
  if (err == ESP_OK) {
    espnow_init_ok_ = true;
    ESP_LOGI(TAG, "ESP-NOW initialized successfully");
    last_status_ = "ESP-NOW initialized";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif

    // 6. Register the broadcast peer (all-FF MAC, no encryption)
    esp_now_peer_info_t peerInfo = {};
    memset(peerInfo.peer_addr, 0xFF, 6); // Broadcast address
    peerInfo.channel = channel_;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = 0; // No encryption (broadcast only)
    esp_err_t peer_err = esp_now_add_peer(&peerInfo);
    if (peer_err == ESP_OK || peer_err == ESP_ERR_ESPNOW_EXIST) {
      // Peer registered successfully or already exists
    } else {
      ESP_LOGE(TAG, "Failed to register broadcast peer for ESP-NOW: %d", peer_err);
    }

    // 7. Register or unregister the receive callback depending on whether there are subscriptions
    if (has_subscriptions_) {
      // Register receive callback for incoming ESP-NOW messages
      auto rx_cb = [](const esp_now_recv_info *recv_info, const uint8_t *data, int len) {
        ESP_LOGV(TAG, "[RX_CB] ESP-NOW receive callback triggered");
        if (!recv_info) {
          ESP_LOGE(TAG, "[RX_CB] recv_info is null");
          return;
        }
        if (data == nullptr) {
          ESP_LOGE(TAG, "[RX_CB] data is null");
          return;
        }
        if (len <= 0) {
          ESP_LOGE(TAG, "[RX_CB] len is invalid: %d", len);
          return;
        }
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
                 recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
        ESP_LOGV(TAG, "[RX_CB] Received ESP-NOW packet from %s, len=%d", mac_str, len);
        auto *inst = esphome::espnow_pubsub::global_espnow_pubsub_instance;
        if (inst) {
          ESP_LOGV(TAG, "[RX_CB] Calling on_espnow_receive");
          inst->on_espnow_receive(recv_info, recv_info->src_addr, data, len);
        } else {
          ESP_LOGE(TAG, "[RX_CB] global_espnow_pubsub_instance is null");
        }
      };
      esp_err_t cb_err = esp_now_register_recv_cb(rx_cb);
      if (cb_err == ESP_OK) {
        // Receive callback registered
      } else {
        ESP_LOGE(TAG, "[INIT] Failed to register ESP-NOW receive callback: %d", cb_err);
      }
      // Set global instance pointer for callback
      esphome::espnow_pubsub::global_espnow_pubsub_instance = this;
    } else {
      // No subscriptions: unregister receive callback if previously set
      esp_err_t cb_err = esp_now_unregister_recv_cb();
      // Receive callback unregistered
    }
    // Initialization complete
  } else {
    espnow_init_ok_ = false;
    espnow_init_error_code_ = err;
    ESP_LOGE(TAG, "ESP-NOW initialization failed: %d", err);
    last_status_ = "ESP-NOW init failed: " + std::to_string(err);
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
  }
}

// ESP-NOW initialization for WiFi-managed mode.
// This function is called when ESPHome manages WiFi and the channel is known.
// It ensures ESP-NOW is only initialized if the WiFi channel matches the configured ESP-NOW channel.
//
// Steps:
// 1. Check that the WiFi channel matches the ESP-NOW channel (required for broadcast).
// 2. If channels match, call init_espnow_common() to perform core initialization.
// 3. If channels do not match, log an error and set error code.
void EspNowPubSub::init_espnow_after_wifi(uint8_t wifi_channel) {
  ESP_LOGV(TAG, "[INIT] init_espnow_after_wifi called with wifi_channel=%d, configured channel_=%d", (int)wifi_channel, (int)channel_);
  // Record the channel provided by the WiFi component and determine compatibility
  wifi_component_channel_ = static_cast<int>(wifi_channel);
  wifi_channel_compatible_ = (wifi_channel == channel_);

  if (!wifi_channel_compatible_) {
    ESP_LOGE(TAG, "[ERROR] ESP-NOW channel (%d) does not match WiFi channel (%d)! ESP-NOW will not work.", channel_, wifi_component_channel_);
    espnow_init_error_code_ = ESP_FAIL;
    last_status_ = "ESP-NOW channel mismatch";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    return;
  }
  // Channels match, proceed with ESP-NOW initialization
  init_espnow_common();
}

// Standalone ESP-NOW initialization (no WiFi component present).
// This function is called when ESPHome is not managing WiFi.
// It directly calls init_espnow_common() to set up WiFi and ESP-NOW in standalone mode.
void EspNowPubSub::init_espnow_standalone() {
  // --- Always attempt to initialize WiFi driver (for ESP-NOW with Ethernet) ---
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  esp_err_t init_err = esp_wifi_init(&cfg);
  if (init_err == ESP_OK) {
    ESP_LOGI(TAG, "WiFi driver manually initialized for ESP-NOW use with Ethernet.");
  } else if (init_err == ESP_ERR_WIFI_INIT_STATE) {
    // Already initialized, not an error
    ESP_LOGI(TAG, "WiFi driver already initialized (ESP_ERR_WIFI_INIT_STATE), continuing.");
  } else {
    ESP_LOGE(TAG, "Failed to manually initialize WiFi driver for ESP-NOW: %d", init_err);
    espnow_init_error_code_ = init_err;
    return;
  }
  // Always set WiFi mode to STA and start WiFi for ESP-NOW to work (even with Ethernet)
  esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
  if (mode_err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to set WiFi mode to STA for ESP-NOW: %d", mode_err);
    espnow_init_error_code_ = mode_err;
    return;
  }
  esp_err_t start_err = esp_wifi_start();
  if (start_err != ESP_OK && start_err != ESP_ERR_WIFI_CONN) { // ESP_ERR_WIFI_CONN is not fatal here
    ESP_LOGE(TAG, "Failed to start WiFi for ESP-NOW: %d", start_err);
    espnow_init_error_code_ = start_err;
    return;
  }
  init_espnow_common();
}

// ESP-NOW re-initialization after WiFi events.
// This function is called after certain WiFi events (e.g., AP/STA start/stop) to ensure ESP-NOW continues to operate.
// It deinitializes and reinitializes ESP-NOW, re-registers the broadcast peer, and sets up the receive callback.
//
// Steps:
// 1. Deinitialize ESP-NOW to clear any previous state.
// 2. Reinitialize ESP-NOW.
// 3. Register the broadcast peer (all-FF MAC, no encryption).
// 4. Register the receive callback for incoming messages.
// 5. Set status flags and log results.
void EspNowPubSub::reinit_espnow() {
  ESP_LOGV(TAG, "Re-initializing ESP-NOW after WiFi event");
  // Check WiFi mode before attempting ESP-NOW reinit
  wifi_mode_t mode;
  esp_err_t mode_err = esp_wifi_get_mode(&mode);
  if (mode_err != ESP_OK) {
    ESP_LOGW(TAG, "[REINIT] Could not get WiFi mode, skipping ESP-NOW reinit");
    return;
  }
  ESP_LOGV(TAG, "[REINIT] WiFi mode: %d", (int)mode);
  if (mode != WIFI_MODE_STA && mode != WIFI_MODE_AP && mode != WIFI_MODE_APSTA) {
    ESP_LOGW(TAG, "[REINIT] WiFi mode not compatible for ESP-NOW (mode=%d), skipping reinit", (int)mode);
    return;
  }
  uint8_t channel = 0;
  esp_wifi_get_channel(&channel, nullptr);
  ESP_LOGV(TAG, "[REINIT] WiFi channel: %d", (int)channel);
  // 1. Deinitialize ESP-NOW to clear any previous state
  esp_now_deinit();
  // 2. Reinitialize ESP-NOW
  esp_err_t err = esp_now_init();
  if (err == ESP_OK) {
    espnow_init_ok_ = true;
    ESP_LOGV(TAG, "ESP-NOW re-initialized successfully");

    // 3. Register the broadcast peer (all-FF MAC, no encryption)
    esp_now_peer_info_t peerInfo = {};
    memset(peerInfo.peer_addr, 0xFF, 6); // Broadcast address
    peerInfo.channel = channel_;
    peerInfo.ifidx = WIFI_IF_STA;
    peerInfo.encrypt = 0; // No encryption (broadcast only)
    esp_err_t peer_err = esp_now_add_peer(&peerInfo);
    if (peer_err == ESP_OK || peer_err == ESP_ERR_ESPNOW_EXIST) {
      ESP_LOGV(TAG, "Broadcast peer registered for ESP-NOW");
    } else {
      ESP_LOGE(TAG, "Failed to register broadcast peer for ESP-NOW: %d", peer_err);
    }

    // 4. Register the receive callback for incoming messages
    auto rx_cb = [](const esp_now_recv_info *recv_info, const uint8_t *data, int len) {
      ESP_LOGV(TAG, "[RX_CB] ESP-NOW receive callback triggered");
      if (!recv_info) {
        ESP_LOGE(TAG, "[RX_CB] recv_info is null");
        return;
      }
      if (data == nullptr) {
        ESP_LOGE(TAG, "[RX_CB] data is null");
        return;
      }
      if (len <= 0) {
        ESP_LOGE(TAG, "[RX_CB] len is invalid: %d", len);
        return;
      }
      char mac_str[18];
      snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
               recv_info->src_addr[0], recv_info->src_addr[1], recv_info->src_addr[2],
               recv_info->src_addr[3], recv_info->src_addr[4], recv_info->src_addr[5]);
      ESP_LOGV(TAG, "[RX_CB] Received ESP-NOW packet from %s, len=%d", mac_str, len);
      auto *inst = esphome::espnow_pubsub::global_espnow_pubsub_instance;
      if (inst) {
        ESP_LOGV(TAG, "[RX_CB] Calling on_espnow_receive");
        inst->on_espnow_receive(recv_info, recv_info->src_addr, data, len);
      } else {
        ESP_LOGE(TAG, "[RX_CB] global_espnow_pubsub_instance is null");
      }
    };
    esp_err_t cb_err = esp_now_register_recv_cb(rx_cb);
    if (cb_err == ESP_OK) {
      ESP_LOGV(TAG, "[INIT] ESP-NOW receive callback registered successfully");
    } else {
      ESP_LOGE(TAG, "[INIT] Failed to register ESP-NOW receive callback: %d", cb_err);
    }
    // Set global instance pointer for callback
    esphome::espnow_pubsub::global_espnow_pubsub_instance = this;
  } else {
    espnow_init_ok_ = false;
    ESP_LOGE(TAG, "ESP-NOW re-initialization failed: %d", err);
  }
}

// Static WiFi event handler
// wifi_event_handler(): Static callback for WiFi events.
// Handles WiFi connect/disconnect and triggers ESP-NOW (re)initialization as needed.
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
  ESP_LOGV(TAG, "[HANDLER] wifi_event_handler called: event_base=%s, event_id=%ld", event_base == WIFI_EVENT ? "WIFI_EVENT" : "OTHER", (long)event_id);
  auto *inst = static_cast<EspNowPubSub*>(arg);
  if (!inst) {
    ESP_LOGW(TAG, "[HANDLER] wifi_event_handler: inst is null");
    return;
  }
  // Only initialize ESP-NOW after WiFi is connected and channel is valid
  if (event_base == WIFI_EVENT) {
    switch (event_id) {
      case WIFI_EVENT_STA_CONNECTED:
        {
          uint8_t channel = 0;
          esp_wifi_get_channel(&channel, nullptr);
        ESP_LOGV(TAG, "[EVENT] WiFi connected, channel: %d", (int)channel);
          if (channel > 0) {
            ESP_LOGV(TAG, "[HANDLER] Calling init_espnow_after_wifi with channel %d", (int)channel);
            inst->init_espnow_after_wifi(channel);
          } else {
            ESP_LOGV(TAG, "[EVENT] WiFi connected but channel is 0, ESP-NOW not initialized yet.");
          }
        }
        break;
      case WIFI_EVENT_STA_DISCONNECTED:
      case WIFI_EVENT_AP_START:
      case WIFI_EVENT_AP_STOP:
      case WIFI_EVENT_STA_START:
      case WIFI_EVENT_STA_STOP:
        ESP_LOGV(TAG, "[HANDLER] Calling reinit_espnow for event_id=%ld", (long)event_id);
        inst->reinit_espnow();
        break;
      default:
        ESP_LOGV(TAG, "[HANDLER] Unhandled WiFi event_id=%ld", (long)event_id);
        break;
    }
  }
}

// loop(): Called by ESPHome main loop.
// Processes queued ESP-NOW messages and disables itself when idle.
void EspNowPubSub::loop() {
  static bool pending_sensor_update = false;

  // If there are messages, process them and set flag for sensor update next loop
  if (!message_queue_.empty()) {
    std::vector<QueuedMessage> local_queue;
    local_queue.swap(message_queue_);
    for (const auto &msg : local_queue) {
      ESP_LOGD(TAG, "[LOOP] Processing queued ESP-NOW message: topic='%s', payload='%s'", msg.topic.c_str(), msg.payload.c_str());
      receive_message(msg.topic, msg.payload);
    }
    pending_sensor_update = true;
    // Keep loop enabled for next run
    return;
  }

  // If no messages but sensor update is pending, publish sensor states
  if (pending_sensor_update) {
#ifdef USE_SENSOR
    if (rssi_sensor_) rssi_sensor_->publish_state(last_rssi_);
    if (received_count_sensor_) received_count_sensor_->publish_state(received_count_);
#endif
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    pending_sensor_update = false;
    // Keep loop enabled for next run (in case more messages arrive)
    return;
  }

  // If nothing to do, disable loop
  ESP_LOGD(TAG, "[LOOP] No messages or sensor updates to process, disabling loop");
  disable_loop();
  //ESP_LOGD(TAG, "EspNowPubSub loop running");
}

// publish(): Called to send a message to all ESP-NOW peers (broadcast).
// Formats the message as topic\0payload and sends via ESP-NOW.
void EspNowPubSub::publish(const std::string &topic, const std::string &payload) {
  ESP_LOGI(TAG, "Publishing message: topic='%s', payload='%s'", topic.c_str(), payload.c_str());
  if (!espnow_init_ok_) {
    ESP_LOGE(TAG, "ESP-NOW not initialized (espnow_init_ok_ is false, error code: %d), cannot send message", static_cast<int>(espnow_init_error_code_));
    last_status_ = "ESP-NOW not initialized (code: " + std::to_string(espnow_init_error_code_) + ")";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    return;
  }
  // Format: topic\0payload
  std::string msg = topic + '\0' + payload;
  uint8_t broadcast_mac[6];
  memset(broadcast_mac, 0xFF, 6);
  esp_err_t err = esp_now_send(broadcast_mac, reinterpret_cast<const uint8_t *>(msg.data()), msg.size());
  if (err != ESP_OK) {
    // Print error as both decimal and hex for easier ESP-IDF lookup
    ESP_LOGE(TAG, "ESP-NOW send failed: %d (0x%04X)", err, (unsigned)err);
    switch (err) {
      case ESP_ERR_ESPNOW_NOT_INIT:
        last_status_ = "Send failed: ESP-NOW not initialized (ESP_ERR_ESPNOW_NOT_INIT)";
        break;
      case ESP_ERR_ESPNOW_ARG:
        last_status_ = "Send failed: Invalid argument (ESP_ERR_ESPNOW_ARG)";
        break;
      case ESP_ERR_ESPNOW_INTERNAL:
        last_status_ = "Send failed: Internal error (ESP_ERR_ESPNOW_INTERNAL)";
        break;
      case ESP_ERR_ESPNOW_NO_MEM:
        last_status_ = "Send failed: Out of memory (ESP_ERR_ESPNOW_NO_MEM)";
        break;
      case ESP_ERR_ESPNOW_NOT_FOUND:
        last_status_ = "Send failed: Peer not found (ESP_ERR_ESPNOW_NOT_FOUND)";
        break;
      default:
        last_status_ = "Send failed: " + std::to_string(err);
        break;
    }
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
  } else {
    sent_count_++;
#ifdef USE_SENSOR
    if (sent_count_sensor_) sent_count_sensor_->publish_state(sent_count_);
#endif
    last_status_ = "OK";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
  }
}

// on_espnow_receive(): Called from the ESP-NOW receive callback (ISR context).
// Parses the incoming message and queues it for processing in the main loop.
// If the queue is full, drops the oldest message and logs a warning.
void EspNowPubSub::on_espnow_receive(const esp_now_recv_info *recv_info, const uint8_t *mac_addr, const uint8_t *data, int len) {
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
  ESP_LOGV(TAG, "[ON_RX] on_espnow_receive called from %s, len=%d", mac_str, len);
  if (data == nullptr) {
    ESP_LOGE(TAG, "[ON_RX] data is null");
    last_status_ = "RX error: null data";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    return;
  }
  if (len <= 0) {
    ESP_LOGE(TAG, "[ON_RX] len is invalid: %d", len);
    last_status_ = "RX error: invalid len";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    return;
  }
  // Parse topic\0payload
  const char *raw = reinterpret_cast<const char *>(data);
  int topic_len = strnlen(raw, len);
  if (topic_len >= len - 1) {
    ESP_LOGE(TAG, "[ON_RX] Malformed ESP-NOW message: topic_len=%d, len=%d", topic_len, len);
    last_status_ = "RX error: malformed message";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    return;
  }
  std::string topic(raw, topic_len);
  std::string payload(raw + topic_len + 1, len - topic_len - 1);
  ESP_LOGV(TAG, "[ON_RX] Queuing topic='%s', payload='%s' for processing in loop", topic.c_str(), payload.c_str());
  // Message queue overflow handling: drop oldest if full
  if (message_queue_.size() >= MAX_QUEUE_SIZE) {
    ESP_LOGW(TAG, "[ON_RX] Message queue full (%u), dropping oldest message", (unsigned)MAX_QUEUE_SIZE);
    last_status_ = "RX warning: queue full, dropped oldest";
#ifdef USE_TEXT_SENSOR
    if (status_text_sensor_) status_text_sensor_->publish_state(last_status_);
#endif
    message_queue_.erase(message_queue_.begin());
  }
  message_queue_.push_back({topic, payload});
  // Only update values here; actual sensor publishing happens in loop()
#ifdef USE_SENSOR
  if (recv_info && recv_info->rx_ctrl) {
    last_rssi_ = recv_info->rx_ctrl->rssi;
  }
#endif
  received_count_++;
  last_status_ = "OK";
  // If the loop is not already enabled, enable it now
  enable_loop_soon_any_context();
}

// receive_message(): Called from the main loop to process a queued message.
// Matches the topic against all subscriptions (with wildcards) and triggers callbacks.
void EspNowPubSub::receive_message(const std::string &topic, const std::string &payload) {
  bool matched = false;
  for (const auto &sub : subscriptions_) {
    if (mqtt_topic_matches(sub.topic, topic)) {
      ESP_LOGI(TAG, "Received message: topic='%s', payload='%s' [MATCHED SUB: %s]", topic.c_str(), payload.c_str(), sub.topic.c_str());
      matched = true;
      sub.callback(topic, payload);
    }
  }
  if (!matched) {
    ESP_LOGI(TAG, "Received message: topic='%s', payload='%s' [NOT SUBSCRIBED]", topic.c_str(), payload.c_str());
  }
}

// dump_config(): Called by ESPHome to print component configuration and diagnostics.
// Logs MAC address, channel, power save mode, and all current subscriptions.
void EspNowPubSub::dump_config() {
  ESP_LOGCONFIG(TAG, "ESP-NOW PubSub:");
  ESP_LOGCONFIG(TAG, "  MAC Address: %s", mac_address_.c_str());
  ESP_LOGCONFIG(TAG, "  Channel: %d", channel_);
  ESP_LOGCONFIG(TAG, "  WiFi Component Channel: %d", wifi_component_channel_);
  ESP_LOGCONFIG(TAG, "  WiFi Channel Compatible: %s", wifi_channel_compatible_ ? "YES" : "NO");
  if (!wifi_channel_compatible_) {
    ESP_LOGE(TAG, "  [ERROR] ESP-NOW channel (%d) does not match WiFi channel (%d)!", channel_, wifi_component_channel_);
  }
  // Report current WiFi power save mode
  wifi_ps_type_t ps_mode = WIFI_PS_NONE;
  esp_err_t ps_err = esp_wifi_get_ps(&ps_mode);
  const char *ps_str = "UNKNOWN";
  if (ps_err == ESP_OK) {
    switch (ps_mode) {
      case WIFI_PS_NONE: ps_str = "NONE (Power Save Disabled)"; break;
      case WIFI_PS_MIN_MODEM: ps_str = "MIN_MODEM (Modem Sleep)"; break;
      case WIFI_PS_MAX_MODEM: ps_str = "MAX_MODEM (Max Power Save)"; break;
      default: ps_str = "UNKNOWN"; break;
    }
  }
  ESP_LOGCONFIG(TAG, "  WiFi Power Save: %s", ps_str);
  if (espnow_init_ok_) {
    ESP_LOGCONFIG(TAG, "  ESP-NOW: initialized successfully");
  } else {
    ESP_LOGE(TAG, "  [ERROR] ESP-NOW initialization failed (code %d)", static_cast<int>(espnow_init_error_code_));
  }
  for (const auto &sub : subscriptions_) {
    ESP_LOGCONFIG(TAG, "   - Subscribed to topic: %s", sub.topic.c_str());
  }

#ifdef USE_SENSOR
  if (rssi_sensor_) {
    ESP_LOGCONFIG(TAG, "  Sensor: RSSI (signal strength) configured");
  }
  if (sent_count_sensor_) {
    ESP_LOGCONFIG(TAG, "  Sensor: Sent Count configured");
  }
  if (received_count_sensor_) {
    ESP_LOGCONFIG(TAG, "  Sensor: Received Count configured");
  }
#endif
#ifdef USE_TEXT_SENSOR
  if (status_text_sensor_) {
    ESP_LOGCONFIG(TAG, "  Text Sensor: Status configured");
  }
#endif
}

// add_subscription(): Called during setup to add a topic subscription.
// Registers a callback for the given topic (supports wildcards).
void EspNowPubSub::add_subscription(const std::string &topic, OnMessageTrigger *trigger) {
  subscriptions_.push_back({topic, [trigger](const std::string &topic, const std::string &payload) {
    trigger->trigger(topic, payload);
  }});
  has_subscriptions_ = true;
}

// OnMessageTrigger implementation (stub)
OnMessageTrigger::OnMessageTrigger(EspNowPubSub *parent, const std::string &topic) {}

// EspnowPubSubPublishAction implementation

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
void EspnowPubSubPublishAction<Ts...>::play(Ts... x) {
  auto topic = this->topic_.value(x...);
  auto payload = this->payload_.value(x...);
  ESP_LOGD("espnow_pubsub", "[DIAG] EspnowPubSubPublishAction::play called. topic='%s', payload='%s'", topic.c_str(), payload.c_str());
  auto *parent = parent_ != nullptr ? parent_ : global_espnow_pubsub_instance;
  if (parent != nullptr) {
    ESP_LOGV("espnow_pubsub", "[DIAG] parent pointer is valid, calling publish().");
    parent->publish(topic, payload);
  } else {
    ESP_LOGE("espnow_pubsub", "[DIAG] parent pointer is null! Publish action will not execute.");
  }
}

// Explicit template instantiations for common action argument signatures
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

// Place this at the end of the file, after all class and function definitions
namespace esphome {
namespace espnow_pubsub {
EspNowPubSub *global_espnow_pubsub_instance = nullptr;
}
}