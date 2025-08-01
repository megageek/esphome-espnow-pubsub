# MIT License
# Copyright (c) 2025 Mark Johnson
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# Import sensor and text_sensor modules for registration helpers
from esphome.components import sensor, text_sensor
# Placeholder for espnow_pubsub component Python integration
# (Renamed from pubsub_espnow)

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_CHANNEL,
    CONF_TOPIC,
    CONF_TRIGGER_ID,
)
from esphome.components.esp32 import add_idf_sdkconfig_option
from esphome.core import CORE

MULTI_CONF = True

espnow_pubsub_ns = cg.esphome_ns.namespace("espnow_pubsub")
EspNowPubSub = espnow_pubsub_ns.class_("EspNowPubSub", cg.Component)
# Triggers
OnMessageTrigger = espnow_pubsub_ns.class_("OnMessageTrigger", automation.Trigger.template(cg.std_string, cg.std_string))
# Actions
EspnowPubSubPublishAction = espnow_pubsub_ns.class_("EspnowPubSubPublishAction", automation.Action)

ON_MESSAGE_SCHEMA = automation.validate_automation(
    {
        cv.GenerateID(CONF_TRIGGER_ID): cv.declare_id(OnMessageTrigger),
        cv.Required(CONF_TOPIC): cv.string,
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(EspNowPubSub),
        cv.Required(CONF_CHANNEL): cv.int_range(1, 14),
        cv.Optional("on_message"): cv.ensure_list(ON_MESSAGE_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)


async def espnow_pubsub_sensor_to_code(config):
    parent = await cg.get_variable(config[CONF_ID])
    if "rssi" in config:
        sens = await sensor.new_sensor(config["rssi"])
        await sensor.register_sensor(sens, config["rssi"])
        cg.add(parent.set_rssi_sensor(sens))
    if "sent_count" in config:
        sens = await sensor.new_sensor(config["sent_count"])
        await sensor.register_sensor(sens, config["sent_count"])
        cg.add(parent.set_sent_count_sensor(sens))
    if "received_count" in config:
        sens = await sensor.new_sensor(config["received_count"])
        await sensor.register_sensor(sens, config["received_count"])
        cg.add(parent.set_received_count_sensor(sens))

async def espnow_pubsub_text_sensor_to_code(config):
    parent = await cg.get_variable(config[CONF_ID])
    if "status_text" in config:
        tsens = await text_sensor.new_text_sensor(config["status_text"])
        await text_sensor.register_text_sensor(tsens, config["status_text"])
        cg.add(parent.set_status_text_sensor(tsens))


@automation.register_action(
    "espnow_pubsub.publish",
    EspnowPubSubPublishAction,
    cv.Schema(
        {
            cv.Required(CONF_ID): cv.use_id(EspNowPubSub),
            cv.Required(CONF_TOPIC): cv.string,
            cv.Required("payload"): cv.string,
        }
    ),
)
async def espnow_pubsub_publish_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, await cg.get_variable(config[CONF_ID]))
    cg.add(var.set_topic(config[CONF_TOPIC]))
    cg.add(var.set_payload(config["payload"]))
    return var

async def to_code(config):
    cg.add_define("USE_ESPNOW_PUBSUB")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_channel(config[CONF_CHANNEL]))

    for conf in config.get("on_message", []):
        # Fix: conf may be a list if schema is not flattened
        if isinstance(conf, list):
            for sub_conf in conf:
                trigger = cg.new_Pvariable(sub_conf[CONF_TRIGGER_ID], var, sub_conf[CONF_TOPIC])
                cg.add(var.add_subscription(sub_conf[CONF_TOPIC], trigger))
                await automation.build_automation(trigger, [(cg.std_string, "topic"), (cg.std_string, "payload")], sub_conf)
        else:
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var, conf[CONF_TOPIC])
            cg.add(var.add_subscription(conf[CONF_TOPIC], trigger))
            await automation.build_automation(trigger, [(cg.std_string, "topic"), (cg.std_string, "payload")], conf)

    if CORE.using_esp_idf:
        # Ensure WiFi driver is enabled for ESP-IDF
        add_idf_sdkconfig_option("CONFIG_ESP_WIFI_ENABLED", True)
        add_idf_sdkconfig_option("CONFIG_SW_COEXIST_ENABLE", True)

# Sensor and text_sensor platform registration and codegen have been moved to sensor.py and text_sensor.py
