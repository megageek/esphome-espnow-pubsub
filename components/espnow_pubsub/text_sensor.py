import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
from . import espnow_pubsub_ns, EspNowPubSub

DEPENDANCIES = ["espnow_pubsub"]

ESP_NOW_TEXT_SENSOR_SCHEMA = text_sensor.text_sensor_schema()

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(EspNowPubSub),
        cv.Optional("status_text"): ESP_NOW_TEXT_SENSOR_SCHEMA,
    }
)

async def to_code(config):
    parent = await cg.get_variable(config["id"])
    if "status_text" in config:
        tsens = await text_sensor.new_text_sensor(config["status_text"])
        await text_sensor.register_text_sensor(tsens, config["status_text"])
        cg.add(parent.set_status_text_sensor(tsens))
