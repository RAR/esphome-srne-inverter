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
CONF_SOC_DISCHARGE_ALARM = "soc_discharge_alarm"
CONF_SOC_DISCHARGE_CUTOFF = "soc_discharge_cutoff"
CONF_SOC_CHARGE_CUTOFF = "soc_charge_cutoff"
CONF_SOC_SWITCH_TO_MAINS = "soc_switch_to_mains"
CONF_SOC_SWITCH_TO_INVERTER = "soc_switch_to_inverter"

# Register addresses + per-register defaults (kept in sync with REG_* in cpp)
REG_MAX_CHARGE_CURRENT = 0xE20A          # 0..200 A per manual item 07, scale 0.1
REG_MAINS_CHARGE_CURRENT_LIMIT = 0xE205  # 0..100 A on Anenji firmware (manual says 0..120 but firmware rejects > 100), scale 0.1
REG_OUTPUT_VOLTAGE = 0xE208              # 100..264 V, scale 0.1

# SOC thresholds (per §5.2 menu items 58-62; addresses tentative — calibrated
# from scan values matching defaults; please verify against your firmware).
REG_SOC_DISCHARGE_ALARM = 0xE01E    # menu 58, default 15%
REG_SOC_DISCHARGE_CUTOFF = 0xE00F   # menu 59, default 5%
REG_SOC_CHARGE_CUTOFF = 0xE01D      # menu 60, default 100%
REG_SOC_SWITCH_TO_MAINS = 0xE01F    # menu 61, default 10%
REG_SOC_SWITCH_TO_INVERTER = 0xE020 # menu 62, default 100%

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
        cv.Optional(CONF_SOC_DISCHARGE_ALARM): number.number_schema(
            SrneNumber,
            unit_of_measurement="%",
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:battery-alert",
        ),
        cv.Optional(CONF_SOC_DISCHARGE_CUTOFF): number.number_schema(
            SrneNumber,
            unit_of_measurement="%",
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:battery-off",
        ),
        cv.Optional(CONF_SOC_CHARGE_CUTOFF): number.number_schema(
            SrneNumber,
            unit_of_measurement="%",
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:battery-charging-100",
        ),
        cv.Optional(CONF_SOC_SWITCH_TO_MAINS): number.number_schema(
            SrneNumber,
            unit_of_measurement="%",
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:transmission-tower-import",
        ),
        cv.Optional(CONF_SOC_SWITCH_TO_INVERTER): number.number_schema(
            SrneNumber,
            unit_of_measurement="%",
            entity_category=ENTITY_CATEGORY_CONFIG,
            icon="mdi:home-battery",
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
            max_value=200,
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

    soc_specs = [
        (CONF_SOC_DISCHARGE_ALARM, REG_SOC_DISCHARGE_ALARM, hub.set_soc_discharge_alarm_number),
        (CONF_SOC_DISCHARGE_CUTOFF, REG_SOC_DISCHARGE_CUTOFF, hub.set_soc_discharge_cutoff_number),
        (CONF_SOC_CHARGE_CUTOFF, REG_SOC_CHARGE_CUTOFF, hub.set_soc_charge_cutoff_number),
        (CONF_SOC_SWITCH_TO_MAINS, REG_SOC_SWITCH_TO_MAINS, hub.set_soc_switch_to_mains_number),
        (CONF_SOC_SWITCH_TO_INVERTER, REG_SOC_SWITCH_TO_INVERTER, hub.set_soc_switch_to_inverter_number),
    ]
    for key, reg, setter in soc_specs:
        if key in config:
            n = await _make_number(
                config[key], min_value=0, max_value=100, step=1,
                register_addr=reg, scale=1.0, hub=hub,
            )
            cg.add(setter(n))
