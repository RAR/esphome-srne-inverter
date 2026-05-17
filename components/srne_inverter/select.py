import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID, srne_inverter_ns

DEPENDENCIES = ["srne_inverter"]

SrneSelect = srne_inverter_ns.class_("SrneSelect", select.Select, cg.Component)

CONF_OUTPUT_PRIORITY = "output_priority"
CONF_CHARGE_PRIORITY = "charge_priority"

# Order MUST match the inverter's enum values — index in this list is the raw
# value written to the register.
#   0xE204 Output priority: 0 solar, 1 line, 2 SBU
OUTPUT_PRIORITY_OPTIONS = ["Solar", "Line", "SBU"]
#   0xE20F Charge priority: 0 PV preferred, 1 Mains preferred, 2 Hybrid, 3 PV only
CHARGE_PRIORITY_OPTIONS = ["PV preferred", "Mains preferred", "Hybrid", "PV only"]

# Register addresses (kept in sync with srne_inverter.cpp REG_* constants)
REG_OUTPUT_PRIORITY = 0xE204
REG_CHARGE_PRIORITY = 0xE20F

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_OUTPUT_PRIORITY): select.select_schema(
            SrneSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:transmission-tower-export",
        ),
        cv.Optional(CONF_CHARGE_PRIORITY): select.select_schema(
            SrneSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:solar-power",
        ),
    }
)


async def _make_select(config, options, register_addr, hub):
    var = await select.new_select(config, options=options)
    await cg.register_component(var, config)
    cg.add(var.set_parent(hub))
    cg.add(var.set_register(register_addr))
    return var


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    if CONF_OUTPUT_PRIORITY in config:
        sel = await _make_select(
            config[CONF_OUTPUT_PRIORITY], OUTPUT_PRIORITY_OPTIONS, REG_OUTPUT_PRIORITY, hub
        )
        cg.add(hub.set_output_priority_select(sel))

    if CONF_CHARGE_PRIORITY in config:
        sel = await _make_select(
            config[CONF_CHARGE_PRIORITY], CHARGE_PRIORITY_OPTIONS, REG_CHARGE_PRIORITY, hub
        )
        cg.add(hub.set_charge_priority_select(sel))
