import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    ble_client, sensor,
    select, binary_sensor,
    climate, number, text_sensor
)
from esphome.const import (
    CONF_ID,CONF_STATE, CONF_PIN,
    CONF_VERSION, UNIT_CELSIUS,
    ICON_THERMOMETER, ICON_FLASH,
    ENTITY_CATEGORY_DIAGNOSTIC,
    UNIT_KILOWATT_HOURS,
    STATE_CLASS_TOTAL_INCREASING,
    DEVICE_CLASS_ENERGY
)

AUTO_LOAD = ["sensor", "binary_sensor", "select", "climate", "esp32_ble_tracker", "number", "text_sensor"]
MULTI_CONF = 3

CONF_TEMP1 = 'temp1'
CONF_TEMP2 = 'temp2'
CONF_MODE = 'mode'
CONF_HDO_LOW_TARIFF = 'hdo_low_tariff'
CONF_HEAT_ON = 'heat_on'
CONF_THERMOSTAT = 'thermostat'
CONF_CONSUMPTION = 'consumption'
CONF_BNAME = "b_name"

smartboiler_controller_ns = cg.esphome_ns.namespace('sb')

SmartBoiler = smartboiler_controller_ns.class_(
    'SmartBoiler', cg.PollingComponent, ble_client.BLEClientNode)

SmartBoilerModeSelect = smartboiler_controller_ns.class_('SmartBoilerModeSelect', select.Select)
SmartBoilerThermostat = smartboiler_controller_ns.class_('SmartBoilerThermostat', climate.Climate)
SmartBoilerPinInput = smartboiler_controller_ns.class_('SmartBoilerPinInput', number.Number)

CONFIG_SCHEMA = cv.polling_component_schema('600s').extend({
    cv.GenerateID(): cv.declare_id(SmartBoiler),
    cv.Optional(CONF_TEMP1): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, accuracy_decimals=1).extend(),
    cv.Optional(CONF_TEMP2): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, accuracy_decimals=1).extend(),
    cv.Optional(CONF_HDO_LOW_TARIFF, {"name": "HDO"}): binary_sensor.binary_sensor_schema().extend(),
    cv.Optional(CONF_HEAT_ON, {"name":"Heating", "icon": "mdi:radiator"}): binary_sensor.binary_sensor_schema().extend(),
    cv.Optional(CONF_MODE): select.SELECT_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(SmartBoilerModeSelect),
    }),
    cv.Optional(CONF_THERMOSTAT): climate.CLIMATE_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(SmartBoilerThermostat),
    }),
    cv.Optional(CONF_PIN, {"name": "Pairing PIN", "mode": "BOX", "icon": "mdi:lock"}): number.NUMBER_SCHEMA.extend({
        cv.GenerateID(): cv.declare_id(SmartBoilerPinInput),
    }),
    cv.Optional(CONF_STATE, {"name": "State", "icon": "mdi:connection" }): text_sensor.text_sensor_schema().extend(),
    cv.Optional(CONF_VERSION, {"name": "Version", "entity_category": ENTITY_CATEGORY_DIAGNOSTIC }): text_sensor.text_sensor_schema().extend(),
    cv.Optional(CONF_BNAME, {"name": "Name" }): text_sensor.text_sensor_schema().extend(),
    cv.Optional(CONF_CONSUMPTION, {"name": "Energy"}): sensor.sensor_schema(
            unit_of_measurement=UNIT_KILOWATT_HOURS,
            icon=ICON_FLASH,
            accuracy_decimals=3,
            state_class=STATE_CLASS_TOTAL_INCREASING,
            device_class=DEVICE_CLASS_ENERGY).extend(),
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
        sel = await select.new_select(config[CONF_MODE], options=['ANTIFREEZE', 'SMART', 'PROG', 'MANUAL'])
        cg.add(var.set_mode(sel))
        await cg.register_parented(sel, var)

    if CONF_THERMOSTAT in config:
        therm = cg.new_Pvariable(config[CONF_THERMOSTAT][CONF_ID])
        await climate.register_climate(therm, config[CONF_THERMOSTAT])
        cg.add(var.set_thermostat(therm))
        await cg.register_parented(therm, var)

    if CONF_PIN in config:
        pin_num = cg.new_Pvariable(config[CONF_PIN][CONF_ID])
        await number.register_number(pin_num, config[CONF_PIN], min_value=1111, max_value=9999, step=1)
        cg.add(var.set_pin_input(pin_num))
        await cg.register_parented(pin_num, var)

    if CONF_STATE in config:
        state = cg.new_Pvariable(config[CONF_STATE][CONF_ID])
        await text_sensor.register_text_sensor(state, config[CONF_STATE])
        cg.add(var.set_state(state))

    if CONF_VERSION in config:
        state = cg.new_Pvariable(config[CONF_VERSION][CONF_ID])
        await text_sensor.register_text_sensor(state, config[CONF_VERSION])
        cg.add(var.set_version(state))

    if CONF_BNAME in config:
        name = cg.new_Pvariable(config[CONF_BNAME][CONF_ID])
        await text_sensor.register_text_sensor(name, config[CONF_BNAME])
        cg.add(var.set_name(name))
