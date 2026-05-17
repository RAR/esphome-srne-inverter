import esphome.codegen as cg
from esphome.components import number
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_CONFIG,
    UNIT_AMPERE,
    UNIT_VOLT,
)

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID, srne_inverter_ns

DEPENDENCIES = ["srne_inverter"]

SrneNumber = srne_inverter_ns.class_("SrneNumber", number.Number, cg.Component)

CONF_MAX_CHARGE_CURRENT = "max_charge_current"
CONF_MAINS_CHARGE_CURRENT_LIMIT = "mains_charge_current_limit"
CONF_OUTPUT_VOLTAGE = "output_voltage"

# Register addresses + per-register defaults (kept in sync with REG_* in cpp)
REG_MAX_CHARGE_CURRENT = 0xE20A          # 0..150 A, scale 0.1
REG_MAINS_CHARGE_CURRENT_LIMIT = 0xE205  # 0..100 A, scale 0.1
REG_OUTPUT_VOLTAGE = 0xE208              # 100..264 V, scale 0.1

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_MAX_CHARGE_CURRENT): number.number_schema(
            SrneNumber,
            unit_of_measurement=UNIT_AMPERE,
            device_class=DEVICE_CLASS_CURRENT,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:current-dc",
        ),
        cv.Optional(CONF_MAINS_CHARGE_CURRENT_LIMIT): number.number_schema(
            SrneNumber,
            unit_of_measurement=UNIT_AMPERE,
            device_class=DEVICE_CLASS_CURRENT,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:transmission-tower-import",
        ),
        cv.Optional(CONF_OUTPUT_VOLTAGE): number.number_schema(
            SrneNumber,
            unit_of_measurement=UNIT_VOLT,
            device_class=DEVICE_CLASS_VOLTAGE,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:sine-wave",
        ),
    }
)


async def _make_number(config, *, min_value, max_value, step, register_addr, scale, hub):
    var = await number.new_number(
        config,
        min_value=min_value,
        max_value=max_value,
        step=step,
    )
    await cg.register_component(var, config)
    cg.add(var.set_parent(hub))
    cg.add(var.set_register(register_addr))
    cg.add(var.set_scale(scale))
    return var


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    if CONF_MAX_CHARGE_CURRENT in config:
        n = await _make_number(
            config[CONF_MAX_CHARGE_CURRENT],
            min_value=0,
            max_value=150,
            step=1,
            register_addr=REG_MAX_CHARGE_CURRENT,
            scale=0.1,
            hub=hub,
        )
        cg.add(hub.set_max_charge_current_number(n))

    if CONF_MAINS_CHARGE_CURRENT_LIMIT in config:
        n = await _make_number(
            config[CONF_MAINS_CHARGE_CURRENT_LIMIT],
            min_value=0,
            max_value=100,
            step=1,
            register_addr=REG_MAINS_CHARGE_CURRENT_LIMIT,
            scale=0.1,
            hub=hub,
        )
        cg.add(hub.set_mains_charge_current_limit_number(n))

    if CONF_OUTPUT_VOLTAGE in config:
        n = await _make_number(
            config[CONF_OUTPUT_VOLTAGE],
            min_value=100,
            max_value=264,
            step=0.1,
            register_addr=REG_OUTPUT_VOLTAGE,
            scale=0.1,
            hub=hub,
        )
        cg.add(hub.set_output_voltage_number(n))
