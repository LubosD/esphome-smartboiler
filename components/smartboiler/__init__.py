import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ble_client, sensor, select, binary_sensor, climate
from esphome.const import CONF_ID,UNIT_CELSIUS, ICON_THERMOMETER, ICON_FLASH

DEPENDENCIES = ['esp32', 'ble_client', 'mqtt', 'sensor', 'binary_sensor', 'select', 'climate']

AUTO_LOAD = []
#MULTI_CONF = True

CONF_TOPIC = 'topic_prefix'

CONF_TEMP1 = 'temp1'
CONF_TEMP2 = 'temp2'
CONF_MODE = 'mode'
CONF_HDO_LOW_TARIFF = 'hdo_low_tariff'
CONF_HEAT_ON = 'heat_on'
CONF_THERMOSTAT = 'thermostat'
CONF_CONSUMPTION = 'consumption'

smartboiler_controller_ns = cg.esphome_ns.namespace('sb')

SmartBoiler = smartboiler_controller_ns.class_(
    'SmartBoiler', cg.PollingComponent, ble_client.BLEClientNode)

SmartBoilerModeSelect = smartboiler_controller_ns.class_('SmartBoilerModeSelect', select.Select)
SmartBoilerThermostat = smartboiler_controller_ns.class_('SmartBoilerThermostat', climate.Climate)

CONFIG_SCHEMA = cv.polling_component_schema('30s').extend({
    cv.GenerateID(): cv.declare_id(SmartBoiler),
    cv.Optional(CONF_TOPIC, "smartboiler"): cv.string,
    cv.Optional(CONF_TEMP1): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, accuracy_decimals=1).extend(),
    cv.Optional(CONF_TEMP2): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, accuracy_decimals=1).extend(),
    cv.Optional(CONF_HDO_LOW_TARIFF): binary_sensor.binary_sensor_schema().extend(),
    cv.Optional(CONF_HEAT_ON): binary_sensor.binary_sensor_schema().extend(),
    cv.Optional(CONF_MODE): select.SELECT_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(SmartBoilerModeSelect),
    }),
    cv.Optional(CONF_THERMOSTAT): climate.CLIMATE_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(SmartBoilerThermostat),
    }),
    cv.Optional(CONF_CONSUMPTION): sensor.sensor_schema(unit_of_measurement="Wh", icon=ICON_FLASH, accuracy_decimals=1).extend(),
}).extend(ble_client.BLE_CLIENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    if CONF_TEMP1 in config:
        sens = await sensor.new_sensor(config[CONF_TEMP1])
        cg.add(var.set_temp1(sens))

    if CONF_TEMP2 in config:
        sens = await sensor.new_sensor(config[CONF_TEMP2])
        cg.add(var.set_temp2(sens))

    if CONF_CONSUMPTION in config:
        sens = await sensor.new_sensor(config[CONF_CONSUMPTION])
        cg.add(var.set_consumption(sens))

    if CONF_HDO_LOW_TARIFF in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_HDO_LOW_TARIFF])
        cg.add(var.set_hdo_low_tariff(sens))

    if CONF_HEAT_ON in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_HEAT_ON])
        cg.add(var.set_heat_on(sens))

    if CONF_MODE in config:
        sel = await select.new_select(config[CONF_MODE], options=['STOP', 'NORMAL', 'HDO', 'SMART', 'SMARTHDO', 'ANTIFROST', 'NIGHT', 'TEST'])
        cg.add(var.set_mode(sel))

    if CONF_THERMOSTAT in config:
        therm = cg.new_Pvariable(config[CONF_THERMOSTAT][CONF_ID])
        await climate.register_climate(therm, config[CONF_THERMOSTAT])
        cg.add(var.set_thermostat(therm))

    cg.add(var.set_root_topic(config[CONF_TOPIC]))

