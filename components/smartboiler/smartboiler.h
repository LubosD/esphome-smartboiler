#ifndef SMARTBOILER_H
#define SMARTBOILER_H

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/mqtt/custom_mqtt_device.h"
//#include "esphome/components/text_sensor/text_sensor.h"
//#include "esphome/components/sensor/sensor.h"
//#include "esphome/components/select/select.h"

namespace esphome {
namespace sb {

class SmartBoiler : public Component, public esphome::ble_client::BLEClientNode, public esphome::mqtt::CustomMQTTDevice
{
public:
	void setup() override;
	//void dump_config() override;
	float get_setup_priority() const override { return setup_priority::DATA; }
	void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

	void set_root_topic(const std::string &value);
private:
	void on_set_temperature(const std::string &payload);
	void on_set_mode(const std::string &payload);
	void on_set_hdo_enabled(const std::string &payload);
	void handle_incoming(const uint8_t *data, uint16_t length);
	void request_value(uint8_t value);
	void send_to_boiler(uint8_t* frame, size_t length);
private:
	bool online_ = false;
	std::string root_topic_;
	// Handle for outgoing requests
	uint16_t char_handle_;

	/*
	sensor::Sensor *temperature_sensor_1_sensor_;
	sensor::Sensor *temperature_sensor_2_sensor_;

	text_sensor::TextSensor *board_information_sensor_;
	text_sensor::TextSensor *tariff_sensor_;

	select::Select *mode_select_;
	*/
};

}
}

#endif
