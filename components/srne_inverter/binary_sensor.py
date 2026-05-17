import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID

DEPENDENCIES = ["srne_inverter"]

CONF_ONLINE_STATUS = "online_status"
CONF_GRID_PRESENT = "grid_present"
CONF_INVERTER_ON = "inverter_on"
CONF_FAULT = "fault"

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_ONLINE_STATUS): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_GRID_PRESENT): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_INVERTER_ON): binary_sensor.binary_sensor_schema(),
        cv.Optional(CONF_FAULT): binary_sensor.binary_sensor_schema(),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    mapping = {
        CONF_ONLINE_STATUS: hub.set_online_status_binary_sensor,
        CONF_GRID_PRESENT: hub.set_grid_present_binary_sensor,
        CONF_INVERTER_ON: hub.set_inverter_on_binary_sensor,
        CONF_FAULT: hub.set_fault_binary_sensor,
    }
    for key, setter in mapping.items():
        if key in config:
            sens = await binary_sensor.new_binary_sensor(config[key])
            cg.add(setter(sens))
