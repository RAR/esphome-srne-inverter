import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    DEVICE_CLASS_CURRENT,
    DEVICE_CLASS_FREQUENCY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLTAGE,
    STATE_CLASS_MEASUREMENT,
    UNIT_AMPERE,
    UNIT_CELSIUS,
    UNIT_HERTZ,
    UNIT_PERCENT,
    UNIT_VOLT,
    UNIT_WATT,
)

from . import SRNE_INVERTER_COMPONENT_SCHEMA, CONF_SRNE_INVERTER_ID

DEPENDENCIES = ["srne_inverter"]

# Block A (controller / PV)
CONF_BATTERY_SOC = "battery_soc"
CONF_BATTERY_VOLTAGE = "battery_voltage"
CONF_BATTERY_CURRENT = "battery_current"
CONF_PV1_VOLTAGE = "pv1_voltage"
CONF_PV1_CURRENT = "pv1_current"
CONF_PV1_POWER = "pv1_power"
CONF_PV2_VOLTAGE = "pv2_voltage"
CONF_PV2_CURRENT = "pv2_current"
CONF_PV2_POWER = "pv2_power"
CONF_PV_TOTAL_POWER = "pv_total_power"
CONF_CHARGE_POWER = "charge_power"

# Block B (inverter)
CONF_BUS_VOLTAGE = "bus_voltage"
CONF_GRID_VOLTAGE = "grid_voltage"
CONF_GRID_CURRENT = "grid_current"
CONF_GRID_FREQUENCY = "grid_frequency"
CONF_INVERTER_VOLTAGE = "inverter_voltage"
CONF_INVERTER_CURRENT = "inverter_current"
CONF_INVERTER_FREQUENCY = "inverter_frequency"
CONF_LOAD_CURRENT = "load_current"
CONF_LOAD_ACTIVE_POWER = "load_active_power"
CONF_LOAD_APPARENT_POWER = "load_apparent_power"
CONF_LOAD_PERCENT = "load_percent"
CONF_BATTERY_CHARGE_CURRENT = "battery_charge_current"
CONF_PV_CHARGE_CURRENT = "pv_charge_current"
CONF_HEATSINK_A_TEMPERATURE = "heatsink_a_temperature"
CONF_HEATSINK_B_TEMPERATURE = "heatsink_b_temperature"
CONF_HEATSINK_C_TEMPERATURE = "heatsink_c_temperature"
CONF_DC_BUS_POSITIVE_VOLTAGE = "dc_bus_positive_voltage"
CONF_DC_BUS_NEGATIVE_VOLTAGE = "dc_bus_negative_voltage"

# L2 (split-phase / parallel-120 second-leg)
CONF_GRID_VOLTAGE_L2 = "grid_voltage_l2"
CONF_INVERTER_VOLTAGE_L2 = "inverter_voltage_l2"
CONF_INVERTER_CURRENT_L2 = "inverter_current_l2"
CONF_LOAD_CURRENT_L2 = "load_current_l2"
CONF_LOAD_ACTIVE_POWER_L2 = "load_active_power_l2"
CONF_LOAD_APPARENT_POWER_L2 = "load_apparent_power_l2"
CONF_LOAD_PERCENT_L2 = "load_percent_l2"

UNIT_VOLT_AMPS = "VA"

VOLTAGE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_VOLT,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_VOLTAGE,
    state_class=STATE_CLASS_MEASUREMENT,
)
CURRENT_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_AMPERE,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_CURRENT,
    state_class=STATE_CLASS_MEASUREMENT,
)
POWER_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_WATT,
    accuracy_decimals=0,
    device_class=DEVICE_CLASS_POWER,
    state_class=STATE_CLASS_MEASUREMENT,
)
PERCENT_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_PERCENT,
    accuracy_decimals=0,
    state_class=STATE_CLASS_MEASUREMENT,
)
FREQUENCY_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_HERTZ,
    accuracy_decimals=2,
    device_class=DEVICE_CLASS_FREQUENCY,
    state_class=STATE_CLASS_MEASUREMENT,
)
APPARENT_POWER_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_VOLT_AMPS,
    accuracy_decimals=0,
    state_class=STATE_CLASS_MEASUREMENT,
)
TEMPERATURE_SCHEMA = sensor.sensor_schema(
    unit_of_measurement=UNIT_CELSIUS,
    accuracy_decimals=1,
    device_class=DEVICE_CLASS_TEMPERATURE,
    state_class=STATE_CLASS_MEASUREMENT,
)

CONFIG_SCHEMA = SRNE_INVERTER_COMPONENT_SCHEMA.extend(
    {
        cv.Optional(CONF_BATTERY_SOC): PERCENT_SCHEMA,
        cv.Optional(CONF_BATTERY_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_BATTERY_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_PV1_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_PV1_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_PV1_POWER): POWER_SCHEMA,
        cv.Optional(CONF_PV2_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_PV2_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_PV2_POWER): POWER_SCHEMA,
        cv.Optional(CONF_PV_TOTAL_POWER): POWER_SCHEMA,
        cv.Optional(CONF_CHARGE_POWER): POWER_SCHEMA,
        cv.Optional(CONF_BUS_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_GRID_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_GRID_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_GRID_FREQUENCY): FREQUENCY_SCHEMA,
        cv.Optional(CONF_INVERTER_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_INVERTER_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_INVERTER_FREQUENCY): FREQUENCY_SCHEMA,
        cv.Optional(CONF_LOAD_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_LOAD_ACTIVE_POWER): POWER_SCHEMA,
        cv.Optional(CONF_LOAD_APPARENT_POWER): APPARENT_POWER_SCHEMA,
        cv.Optional(CONF_LOAD_PERCENT): PERCENT_SCHEMA,
        cv.Optional(CONF_BATTERY_CHARGE_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_PV_CHARGE_CURRENT): CURRENT_SCHEMA,
        cv.Optional(CONF_HEATSINK_A_TEMPERATURE): TEMPERATURE_SCHEMA,
        cv.Optional(CONF_HEATSINK_B_TEMPERATURE): TEMPERATURE_SCHEMA,
        cv.Optional(CONF_HEATSINK_C_TEMPERATURE): TEMPERATURE_SCHEMA,
        cv.Optional(CONF_DC_BUS_POSITIVE_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_DC_BUS_NEGATIVE_VOLTAGE): VOLTAGE_SCHEMA,
        cv.Optional(CONF_GRID_VOLTAGE_L2): VOLTAGE_SCHEMA,
        cv.Optional(CONF_INVERTER_VOLTAGE_L2): VOLTAGE_SCHEMA,
        cv.Optional(CONF_INVERTER_CURRENT_L2): CURRENT_SCHEMA,
        cv.Optional(CONF_LOAD_CURRENT_L2): CURRENT_SCHEMA,
        cv.Optional(CONF_LOAD_ACTIVE_POWER_L2): POWER_SCHEMA,
        cv.Optional(CONF_LOAD_APPARENT_POWER_L2): APPARENT_POWER_SCHEMA,
        cv.Optional(CONF_LOAD_PERCENT_L2): PERCENT_SCHEMA,
    }
)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_SRNE_INVERTER_ID])

    mapping = {
        CONF_BATTERY_SOC: hub.set_battery_soc_sensor,
        CONF_BATTERY_VOLTAGE: hub.set_battery_voltage_sensor,
        CONF_BATTERY_CURRENT: hub.set_battery_current_sensor,
        CONF_PV1_VOLTAGE: hub.set_pv1_voltage_sensor,
        CONF_PV1_CURRENT: hub.set_pv1_current_sensor,
        CONF_PV1_POWER: hub.set_pv1_power_sensor,
        CONF_PV2_VOLTAGE: hub.set_pv2_voltage_sensor,
        CONF_PV2_CURRENT: hub.set_pv2_current_sensor,
        CONF_PV2_POWER: hub.set_pv2_power_sensor,
        CONF_PV_TOTAL_POWER: hub.set_pv_total_power_sensor,
        CONF_CHARGE_POWER: hub.set_charge_power_sensor,
        CONF_BUS_VOLTAGE: hub.set_bus_voltage_sensor,
        CONF_GRID_VOLTAGE: hub.set_grid_voltage_sensor,
        CONF_GRID_CURRENT: hub.set_grid_current_sensor,
        CONF_GRID_FREQUENCY: hub.set_grid_frequency_sensor,
        CONF_INVERTER_VOLTAGE: hub.set_inverter_voltage_sensor,
        CONF_INVERTER_CURRENT: hub.set_inverter_current_sensor,
        CONF_INVERTER_FREQUENCY: hub.set_inverter_frequency_sensor,
        CONF_LOAD_CURRENT: hub.set_load_current_sensor,
        CONF_LOAD_ACTIVE_POWER: hub.set_load_active_power_sensor,
        CONF_LOAD_APPARENT_POWER: hub.set_load_apparent_power_sensor,
        CONF_LOAD_PERCENT: hub.set_load_percent_sensor,
        CONF_BATTERY_CHARGE_CURRENT: hub.set_battery_charge_current_sensor,
        CONF_PV_CHARGE_CURRENT: hub.set_pv_charge_current_sensor,
        CONF_HEATSINK_A_TEMPERATURE: hub.set_heatsink_a_temperature_sensor,
        CONF_HEATSINK_B_TEMPERATURE: hub.set_heatsink_b_temperature_sensor,
        CONF_HEATSINK_C_TEMPERATURE: hub.set_heatsink_c_temperature_sensor,
        CONF_DC_BUS_POSITIVE_VOLTAGE: hub.set_dc_bus_positive_voltage_sensor,
        CONF_DC_BUS_NEGATIVE_VOLTAGE: hub.set_dc_bus_negative_voltage_sensor,
        CONF_GRID_VOLTAGE_L2: hub.set_grid_voltage_l2_sensor,
        CONF_INVERTER_VOLTAGE_L2: hub.set_inverter_voltage_l2_sensor,
        CONF_INVERTER_CURRENT_L2: hub.set_inverter_current_l2_sensor,
        CONF_LOAD_CURRENT_L2: hub.set_load_current_l2_sensor,
        CONF_LOAD_ACTIVE_POWER_L2: hub.set_load_active_power_l2_sensor,
        CONF_LOAD_APPARENT_POWER_L2: hub.set_load_apparent_power_l2_sensor,
        CONF_LOAD_PERCENT_L2: hub.set_load_percent_l2_sensor,
    }
    for key, setter in mapping.items():
        if key in config:
            sens = await sensor.new_sensor(config[key])
            cg.add(setter(sens))
