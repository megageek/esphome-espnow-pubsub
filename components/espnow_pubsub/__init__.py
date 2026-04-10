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

"""ESPNow PubSub component."""

from esphome import automation
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_TOPIC,
    CONF_TRIGGER_ID,
)
from esphome.core import CORE

espnow_pubsub_ns = cg.esphome_ns.namespace("espnow_pubsub")

DEPENDENCIES = ["espnow"]

EspNowPubSub = espnow_pubsub_ns.class_("EspNowPubSub", cg.Component)
# Triggers
OnMessageTrigger = espnow_pubsub_ns.class_(
    "OnMessageTrigger", automation.Trigger.template(cg.std_string, cg.std_string, cg.uint32)
)
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
        cv.Optional("send_times", default=1): cv.int_range(min=1, max=10),
        cv.Optional("on_message"): cv.ensure_list(ON_MESSAGE_SCHEMA),
    }
).extend(cv.COMPONENT_SCHEMA)

@automation.register_action(
    "espnow_pubsub.publish",
    EspnowPubSubPublishAction,
    cv.Schema(
        {
            cv.Required(CONF_TOPIC): cv.templatable(cv.string),
            cv.Required("payload"): cv.templatable(cv.string),
        }
    ),
)
async def espnow_pubsub_publish_action_to_code(config, action_id, template_arg, args):
    # Get the parent instance (the only espnow_pubsub component)
    # Find the only EspNowPubSub id from the global config
    from esphome.const import CONF_ID
    # Find the main espnow_pubsub config block from CORE.config
    main_conf = None
    # Search for espnow_pubsub key specifically
    if "espnow_pubsub" in CORE.config:
        conf = CORE.config["espnow_pubsub"]
        if isinstance(conf, list):
            for c in conf:
                if isinstance(c, dict) and c.get(CONF_ID):
                    main_conf = c
                    break
        elif isinstance(conf, dict):
            main_conf = conf
    if not main_conf:
        import esphome.config_validation as cv
        raise cv.Invalid("No espnow_pubsub instance found. Please declare one in your YAML config.")
    parent = await cg.get_variable(main_conf[CONF_ID])
    var = cg.new_Pvariable(action_id, template_arg, parent)
    topic = await cg.templatable(config["topic"], args, cg.std_string)
    cg.add(var.set_topic(topic))
    payload = await cg.templatable(config["payload"], args, cg.std_string)
    cg.add(var.set_payload(payload))
    return var

async def to_code(config):
    cg.add_define("USE_ESPNOW_PUBSUB")
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_send_times(config["send_times"]))

    for conf in config.get("on_message", []):
        # Fix: conf may be a list if schema is not flattened
        if isinstance(conf, list):
            for sub_conf in conf:
                trigger = cg.new_Pvariable(sub_conf[CONF_TRIGGER_ID], var, sub_conf[CONF_TOPIC])
                cg.add(var.add_subscription(sub_conf[CONF_TOPIC], trigger))
                await automation.build_automation(
                    trigger,
                    [(cg.std_string, "topic"), (cg.std_string, "payload"), (cg.uint32, "sequence")],
                    sub_conf,
                )
        else:
            trigger = cg.new_Pvariable(conf[CONF_TRIGGER_ID], var, conf[CONF_TOPIC])
            cg.add(var.add_subscription(conf[CONF_TOPIC], trigger))
            await automation.build_automation(
                trigger,
                [(cg.std_string, "topic"), (cg.std_string, "payload"), (cg.uint32, "sequence")],
                conf,
            )

# Sensor and text_sensor platform registration and codegen have been moved to sensor.py and text_sensor.py
