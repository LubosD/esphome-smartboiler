#ifndef SMARTBOILER_H
#define SMARTBOILER_H

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/mqtt/custom_mqtt_device.h"
//#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/climate/climate.h"

namespace esphome {
namespace sb {

class SmartBoilerModeSelect;
class SmartBoilerThermostat;

class SmartBoiler : public PollingComponent, public esphome::ble_client::BLEClientNode, public esphome::mqtt::CustomMQTTDevice
{
public:
	SmartBoiler();
	void setup() override;
	void update() override;
	void dump_config() override;
	float get_setup_priority() const override { return setup_priority::DATA; }
	void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

	void set_root_topic(const std::string &value);
	void set_temp1(sensor::Sensor *s) { temperature_sensor_1_sensor_ = s; }
	void set_temp2(sensor::Sensor *s) { temperature_sensor_2_sensor_ = s; }
	void set_hdo_low_tariff(binary_sensor::BinarySensor *s) { hdo_low_tariff_sensor_ = s; }
	void set_heat_on(binary_sensor::BinarySensor *s) { heat_on_sensor_ = s; }
	void set_mode(SmartBoilerModeSelect *s);
	void set_thermostat(SmartBoilerThermostat *t);
	void set_consumption(sensor::Sensor *s) { consumption_sensor_ = s; }
protected:
	void on_set_temperature(const std::string &payload);
	void on_set_temperature_int(int temp);

	void on_set_mode(const std::string &payload);
	void on_set_hdo_enabled(const std::string &payload);
	void handle_incoming(const uint8_t *data, uint16_t length);
	void request_value(uint8_t value, uint8_t uid = 0);
	void send_to_boiler(uint8_t* frame, size_t length);
private:
	bool online_ = false;
	std::string root_topic_;
	// Handle for outgoing requests
	uint16_t char_handle_;
	bool is_stopped_ = false;

	sensor::Sensor *temperature_sensor_1_sensor_ = nullptr;
	sensor::Sensor *temperature_sensor_2_sensor_ = nullptr;
	sensor::Sensor *consumption_sensor_ = nullptr;

	binary_sensor::BinarySensor* hdo_low_tariff_sensor_ = nullptr;
	binary_sensor::BinarySensor* heat_on_sensor_ = nullptr;

	SmartBoilerModeSelect *mode_select_ = nullptr;
	SmartBoilerThermostat *thermostat_ = nullptr;

	/*
	text_sensor::TextSensor *board_information_sensor_;
	text_sensor::TextSensor *tariff_sensor_;
	*/

	friend class SmartBoilerModeSelect;
	friend class SmartBoilerThermostat;
};

class SmartBoilerModeSelect : public esphome::select::Select, public esphome::Parented<SmartBoiler>
{
protected:
	virtual void control(const std::string &value) override;
};

class SmartBoilerThermostat : public esphome::climate::Climate, public esphome::Parented<SmartBoiler>
{
public:
	SmartBoilerThermostat();
protected:
	virtual void control(const esphome::climate::ClimateCall &call) override;
	virtual esphome::climate::ClimateTraits traits() override;

	void publish_target_temp(float temp);
	void publish_current_temp(float temp);
	void publish_action(bool stopped, bool heating);

	friend class SmartBoiler;
};

}
}

#endif
