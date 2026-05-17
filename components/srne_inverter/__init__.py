import esphome.codegen as cg
from esphome.components import srne_modbus
import esphome.config_validation as cv
from esphome.const import CONF_ID

AUTO_LOAD = ["srne_modbus", "binary_sensor", "sensor", "text_sensor", "select", "number", "switch"]
CODEOWNERS = ["@rar"]
MULTI_CONF = True

CONF_SRNE_INVERTER_ID = "srne_inverter_id"

DEFAULT_ADDRESS = 0x01

srne_inverter_ns = cg.esphome_ns.namespace("srne_inverter")
SrneInverter = srne_inverter_ns.class_("SrneInverter", cg.PollingComponent, srne_modbus.SrneModbusDevice)

SRNE_INVERTER_COMPONENT_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_SRNE_INVERTER_ID): cv.use_id(SrneInverter),
    }
)

CONF_SCAN_ON_BOOT = "scan_on_boot"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SrneInverter),
            cv.Optional(CONF_SCAN_ON_BOOT, default=False): cv.boolean,
        }
    )
    .extend(cv.polling_component_schema("10s"))
    .extend(srne_modbus.srne_modbus_device_schema(DEFAULT_ADDRESS))
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await srne_modbus.register_srne_modbus_device(var, config)
    cg.add(var.set_scan_on_boot(config[CONF_SCAN_ON_BOOT]))
