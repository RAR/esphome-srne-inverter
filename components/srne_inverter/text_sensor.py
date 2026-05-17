import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID

DEPENDENCIES = ["srne_inverter"]

CONF_MACHINE_STATE = "machine_state"
CONF_CHARGE_STATE = "charge_state"
CONF_FAULT_CODES = "fault_codes"
CONF_SOFTWARE_VERSION = "software_version"
CONF_HARDWARE_VERSION = "hardware_version"
CONF_SERIAL_NUMBER = "serial_number"

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_MACHINE_STATE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_CHARGE_STATE): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_FAULT_CODES): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_SOFTWARE_VERSION): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_HARDWARE_VERSION): text_sensor.text_sensor_schema(),
        cv.Optional(CONF_SERIAL_NUMBER): text_sensor.text_sensor_schema(),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    mapping = {
        CONF_MACHINE_STATE: hub.set_machine_state_text_sensor,
        CONF_CHARGE_STATE: hub.set_charge_state_text_sensor,
        CONF_FAULT_CODES: hub.set_fault_codes_text_sensor,
        CONF_SOFTWARE_VERSION: hub.set_software_version_text_sensor,
        CONF_HARDWARE_VERSION: hub.set_hardware_version_text_sensor,
        CONF_SERIAL_NUMBER: hub.set_serial_number_text_sensor,
    }
    for key, setter in mapping.items():
        if key in config:
            sens = await text_sensor.new_text_sensor(config[key])
            cg.add(setter(sens))
