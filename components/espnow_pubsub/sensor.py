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
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from . import espnow_pubsub_ns, EspNowPubSub

DEPENDANCIES = ["espnow_pubsub"]

ESP_NOW_SENSOR_SCHEMA = sensor.sensor_schema(
    unit_of_measurement="dBm",
    accuracy_decimals=0,
    device_class="signal_strength",
    state_class="measurement",
)

ESP_NOW_COUNT_SENSOR_SCHEMA = sensor.sensor_schema(
    accuracy_decimals=0,
    state_class="total_increasing",
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(EspNowPubSub),
        cv.Optional("rssi"): ESP_NOW_SENSOR_SCHEMA,
        cv.Optional("sent_count"): ESP_NOW_COUNT_SENSOR_SCHEMA,
        cv.Optional("received_count"): ESP_NOW_COUNT_SENSOR_SCHEMA,
    }
)

async def to_code(config):
    parent = await cg.get_variable(config["id"])
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
