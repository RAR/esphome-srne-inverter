import esphome.codegen as cg
from esphome.components import select
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_CONFIG

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID, srne_inverter_ns

DEPENDENCIES = ["srne_inverter"]

SrneSelect = srne_inverter_ns.class_("SrneSelect", select.Select, cg.Component)

CONF_OUTPUT_PRIORITY = "output_priority"
CONF_CHARGE_PRIORITY = "charge_priority"
CONF_BATTERY_TYPE = "battery_type"

# Order MUST match the inverter's enum values — index in this list is the raw
# value written to the register.
#   0xE204 Output priority: 0 solar, 1 line, 2 SBU
OUTPUT_PRIORITY_OPTIONS = ["Solar", "Line", "SBU"]
#   0xE20F Charge priority: 0 PV preferred, 1 Mains preferred, 2 Hybrid, 3 PV only
CHARGE_PRIORITY_OPTIONS = ["PV preferred", "Mains preferred", "Hybrid", "PV only"]
#   0xE004 Battery type, per §5.2 menu item 08 of the user manual.
#   Indices: 0=User-defined, 1=Sealed lead-acid, 2=Flooded lead-acid, 3=Gel,
#   6,7,8=L14/L15/L16 LFP variants (commonly used here), 4,5,9-12 reserved,
#   13,14=N13/N14 ternary Li-ion. Index defines the wire value.
BATTERY_TYPE_OPTIONS = [
    "User-defined",       # 0
    "Sealed lead-acid",   # 1
    "Flooded lead-acid",  # 2
    "Gel",                # 3
    "Reserved (4)",       # 4
    "Reserved (5)",       # 5
    "LFP L14",            # 6
    "LFP L15",            # 7
    "LFP L16",            # 8
    "Reserved (9)",       # 9
    "Reserved (10)",      # 10
    "Reserved (11)",      # 11
    "Reserved (12)",      # 12
    "Ternary N13",        # 13
    "Ternary N14",        # 14
]

# Register addresses (kept in sync with srne_inverter.cpp REG_* constants)
REG_OUTPUT_PRIORITY = 0xE204
REG_CHARGE_PRIORITY = 0xE20F
REG_BATTERY_TYPE = 0xE004

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
        cv.Optional(CONF_BATTERY_TYPE): select.select_schema(
            SrneSelect,
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:car-battery",
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

    if CONF_BATTERY_TYPE in config:
        sel = await _make_select(
            config[CONF_BATTERY_TYPE], BATTERY_TYPE_OPTIONS, REG_BATTERY_TYPE, hub
        )
        cg.add(hub.set_battery_type_select(sel))
