import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client
from esphome.const import CONF_ID

DEPENDENCIES = ['esp32', 'ble_client', 'mqtt']

AUTO_LOAD = []
#MULTI_CONF = True

CONF_TOPIC = 'topic_prefix'

smartboiler_controller_ns = cg.esphome_ns.namespace('sb')

SmartBoiler = smartboiler_controller_ns.class_(
    'SmartBoiler', cg.Component, ble_client.BLEClientNode)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(SmartBoiler),
    cv.Optional(CONF_TOPIC, "smartboiler"): cv.string,
}).extend(ble_client.BLE_CLIENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    cg.add(var.set_root_topic(config[CONF_TOPIC]))

