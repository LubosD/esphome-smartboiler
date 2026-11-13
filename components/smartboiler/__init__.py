import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import (
    ble_client, sensor,
    select, binary_sensor,
    climate, number, text_sensor, time as time_,
    switch, button,
)
from esphome.const import (
    CONF_ID,CONF_STATE, CONF_PIN,
    CONF_VERSION, UNIT_CELSIUS,
    ICON_THERMOMETER, ICON_FLASH,
    ENTITY_CATEGORY_DIAGNOSTIC,
    UNIT_KILOWATT_HOURS,
    STATE_CLASS_TOTAL_INCREASING,
    DEVICE_CLASS_ENERGY,
    UNIT_VOLT,
    STATE_CLASS_MEASUREMENT,
    ENTITY_CATEGORY_CONFIG,
    DEVICE_CLASS_PROBLEM,
    DEVICE_CLASS_TEMPERATURE,
    DEVICE_CLASS_VOLUME_STORAGE,
    DEVICE_CLASS_DURATION,
    UNIT_LITRE,
)

AUTO_LOAD = ["sensor", "binary_sensor", "select", "climate", "esp32_ble_tracker", "number", "text_sensor",
             "switch", "button"]
MULTI_CONF = 3

CONF_TEMP1 = 'temp1'
CONF_TEMP2 = 'temp2'
CONF_MODE = 'mode'
CONF_HDO_LOW_TARIFF = 'hdo_low_tariff'
CONF_HEAT_ON = 'heat_on'
CONF_THERMOSTAT = 'thermostat'
CONF_CONSUMPTION = 'consumption'
CONF_BNAME = "b_name"
CONF_ANODE_VOLTAGE = 'anode_voltage'
CONF_TIME_ID = 'time_id'
CONF_LOW_TARIFF = 'low_tariff'
CONF_PROBLEM = 'problem'
CONF_ERROR = 'error'
CONF_MODEL = 'model'
CONF_CAPACITY = 'capacity'
CONF_PROG_TEMPERATURE = 'prog_temperature'
CONF_HDO = 'hdo'
CONF_ANTILEGIONELLA = 'antilegionella'
CONF_PROG_TEMP_LOW = 'prog_temp_low'
CONF_PROG_TEMP_HIGH = 'prog_temp_high'
CONF_HOLIDAY_START = 'holiday_start'
CONF_HOLIDAY_LENGTH = 'holiday_length'
CONF_RESET_ENERGY = 'reset_energy'

# keep in sync with smartboiler.h
PROG_MIN_TEMP = 10
PROG_MAX_TEMP = 80
HOLIDAY_MAX_DAYS = 365

smartboiler_controller_ns = cg.esphome_ns.namespace('sb')

SmartBoiler = smartboiler_controller_ns.class_(
    'SmartBoiler', cg.PollingComponent, ble_client.BLEClientNode)

SmartBoilerModeSelect = smartboiler_controller_ns.class_('SmartBoilerModeSelect', select.Select)
SmartBoilerThermostat = smartboiler_controller_ns.class_('SmartBoilerThermostat', climate.Climate)
SmartBoilerPinInput = smartboiler_controller_ns.class_('SmartBoilerPinInput', number.Number)
SmartBoilerSwitch = smartboiler_controller_ns.class_('SmartBoilerSwitch', switch.Switch)
SmartBoilerNumber = smartboiler_controller_ns.class_('SmartBoilerNumber', number.Number)
SmartBoilerResetEnergyButton = smartboiler_controller_ns.class_('SmartBoilerResetEnergyButton', button.Button)
SwitchType = smartboiler_controller_ns.enum('SwitchType', is_class=True)
NumberType = smartboiler_controller_ns.enum('NumberType', is_class=True)

# switches: config key -> (C++ type, setter)
SWITCHES = {
    CONF_HDO: (SwitchType.HDO, 'set_hdo_switch'),
    CONF_ANTILEGIONELLA: (SwitchType.ANTILEGIONELLA, 'set_antilegionella_switch'),
}
# numbers: config key -> (C++ type, setter, min, max)
NUMBERS = {
    CONF_PROG_TEMP_LOW: (NumberType.PROG_TEMP_LOW, 'set_prog_temp_low_number', PROG_MIN_TEMP, PROG_MAX_TEMP),
    CONF_PROG_TEMP_HIGH: (NumberType.PROG_TEMP_HIGH, 'set_prog_temp_high_number', PROG_MIN_TEMP, PROG_MAX_TEMP),
    CONF_HOLIDAY_START: (NumberType.HOLIDAY_START, 'set_holiday_start_number', 0, HOLIDAY_MAX_DAYS),
    CONF_HOLIDAY_LENGTH: (NumberType.HOLIDAY_LENGTH, 'set_holiday_length_number', 0, HOLIDAY_MAX_DAYS),
}

CONFIG_SCHEMA = cv.polling_component_schema('600s').extend({
    cv.GenerateID(): cv.declare_id(SmartBoiler),
    cv.Optional(CONF_TEMP1): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, accuracy_decimals=1).extend(),
    cv.Optional(CONF_TEMP2): sensor.sensor_schema(unit_of_measurement=UNIT_CELSIUS, icon=ICON_THERMOMETER, accuracy_decimals=1).extend(),
    cv.Optional(CONF_HDO_LOW_TARIFF, {"name": "HDO"}): binary_sensor.binary_sensor_schema().extend(),
    cv.Optional(CONF_HEAT_ON, {"name":"Heating", "icon": "mdi:radiator"}): binary_sensor.binary_sensor_schema().extend(),
    cv.Optional(CONF_MODE): select.select_schema(select.Select).extend({
        cv.GenerateID(): cv.declare_id(SmartBoilerModeSelect),
    }),
    cv.Optional(CONF_THERMOSTAT): climate.climate_schema(climate.Climate).extend({
        cv.GenerateID(): cv.declare_id(SmartBoilerThermostat),
    }),
    cv.Optional(CONF_PIN, {"name": "Pairing PIN", "mode": "BOX", "icon": "mdi:lock"}): number.number_schema(number.Number).extend({
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
    cv.Optional(CONF_ANODE_VOLTAGE, {"name": "Anode Voltage"}): sensor.sensor_schema(
            unit_of_measurement=UNIT_VOLT,
            icon=ICON_FLASH,
            accuracy_decimals=3,
            state_class=STATE_CLASS_MEASUREMENT).extend(),
    cv.Optional(CONF_TIME_ID): cv.use_id(time_.RealTimeClock),
    # status
    cv.Optional(CONF_LOW_TARIFF): binary_sensor.binary_sensor_schema(icon="mdi:cash-clock"),
    cv.Optional(CONF_PROBLEM): binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_PROBLEM),
    cv.Optional(CONF_ERROR): text_sensor.text_sensor_schema(
            icon="mdi:alert-circle-outline", entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    cv.Optional(CONF_MODEL): text_sensor.text_sensor_schema(entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    cv.Optional(CONF_CAPACITY): sensor.sensor_schema(
            unit_of_measurement=UNIT_LITRE,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_VOLUME_STORAGE,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC),
    cv.Optional(CONF_PROG_TEMPERATURE): sensor.sensor_schema(
            unit_of_measurement=UNIT_CELSIUS,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_TEMPERATURE,
            state_class=STATE_CLASS_MEASUREMENT),
    # controls (restore mode DISABLED - state always comes from the water heater)
    cv.Optional(CONF_HDO): switch.switch_schema(
            SmartBoilerSwitch, icon="mdi:transmission-tower",
            entity_category=ENTITY_CATEGORY_CONFIG, default_restore_mode="DISABLED"),
    cv.Optional(CONF_ANTILEGIONELLA): switch.switch_schema(
            SmartBoilerSwitch, icon="mdi:bacteria-outline",
            entity_category=ENTITY_CATEGORY_CONFIG, default_restore_mode="DISABLED"),
    cv.Optional(CONF_PROG_TEMP_LOW): number.number_schema(
            SmartBoilerNumber, icon=ICON_THERMOMETER, unit_of_measurement=UNIT_CELSIUS,
            device_class=DEVICE_CLASS_TEMPERATURE, entity_category=ENTITY_CATEGORY_CONFIG),
    cv.Optional(CONF_PROG_TEMP_HIGH): number.number_schema(
            SmartBoilerNumber, icon=ICON_THERMOMETER, unit_of_measurement=UNIT_CELSIUS,
            device_class=DEVICE_CLASS_TEMPERATURE, entity_category=ENTITY_CATEGORY_CONFIG),
    cv.Optional(CONF_HOLIDAY_START): number.number_schema(
            SmartBoilerNumber, icon="mdi:calendar-start", unit_of_measurement="d",
            device_class=DEVICE_CLASS_DURATION),
    cv.Optional(CONF_HOLIDAY_LENGTH): number.number_schema(
            SmartBoilerNumber, icon="mdi:island", unit_of_measurement="d",
            device_class=DEVICE_CLASS_DURATION),
    cv.Optional(CONF_RESET_ENERGY): button.button_schema(
            SmartBoilerResetEnergyButton, icon="mdi:counter", entity_category=ENTITY_CATEGORY_CONFIG),
}).extend(ble_client.BLE_CLIENT_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await ble_client.register_ble_node(var, config)

    if CONF_ANODE_VOLTAGE in config:
        sens = await sensor.new_sensor(config[CONF_ANODE_VOLTAGE])
        cg.add(var.set_anode_voltage(sens))

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
        sel = await select.new_select(config[CONF_MODE], options=['ANTIFREEZE', 'SMART', 'PROG', 'MANUAL', 'STOP', 'HOLIDAY', 'TEST'])
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

    if CONF_TIME_ID in config:
        rtc = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time_clock(rtc))

    # status
    if CONF_LOW_TARIFF in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_LOW_TARIFF])
        cg.add(var.set_low_tariff(sens))

    if CONF_PROBLEM in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_PROBLEM])
        cg.add(var.set_problem(sens))

    if CONF_ERROR in config:
        sens = await text_sensor.new_text_sensor(config[CONF_ERROR])
        cg.add(var.set_error(sens))

    if CONF_MODEL in config:
        sens = await text_sensor.new_text_sensor(config[CONF_MODEL])
        cg.add(var.set_model(sens))

    if CONF_CAPACITY in config:
        sens = await sensor.new_sensor(config[CONF_CAPACITY])
        cg.add(var.set_capacity(sens))

    if CONF_PROG_TEMPERATURE in config:
        sens = await sensor.new_sensor(config[CONF_PROG_TEMPERATURE])
        cg.add(var.set_prog_temperature(sens))

    # controls
    for key, (sw_type, setter) in SWITCHES.items():
        if key in config:
            sw = await switch.new_switch(config[key])
            await cg.register_parented(sw, var)
            cg.add(sw.set_type(sw_type))
            cg.add(getattr(var, setter)(sw))

    for key, (num_type, setter, min_value, max_value) in NUMBERS.items():
        if key in config:
            num = await number.new_number(config[key], min_value=min_value, max_value=max_value, step=1)
            await cg.register_parented(num, var)
            cg.add(num.set_type(num_type))
            cg.add(getattr(var, setter)(num))

    if CONF_RESET_ENERGY in config:
        btn = await button.new_button(config[CONF_RESET_ENERGY])
        await cg.register_parented(btn, var)
