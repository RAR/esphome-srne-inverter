import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID, srne_inverter_ns

DEPENDENCIES = ["srne_inverter"]

SrneSwitch = srne_inverter_ns.class_("SrneSwitch", switch.Switch, cg.Component)

CONF_ECO_MODE = "eco_mode"

REG_ECO_MODE = 0xE20C

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_ECO_MODE): switch.switch_schema(
            SrneSwitch,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:leaf",
        ),
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    if CONF_ECO_MODE in config:
        s = await switch.new_switch(config[CONF_ECO_MODE])
        await cg.register_component(s, config[CONF_ECO_MODE])
        cg.add(s.set_parent(hub))
        cg.add(s.set_register(REG_ECO_MODE))
        cg.add(hub.set_eco_mode_switch(s))
