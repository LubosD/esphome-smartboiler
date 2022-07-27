#include "smartboiler.h"
#include "esphome/core/application.h"

static const char *const TAG = "smartboiler";

namespace esphome {
namespace sb {

// Requests are written to this characteristic
static const uint16_t SB_MAIN_SERVICE_UUID = 0x1899;
static const uint16_t SB_MAIN_CHARACTERISTIC_UUID = 0x2B99;

// Responses arrive through this characteristic
static const uint16_t SB_LOGGING_SERVICE_UUID = 0x1898;
static const uint16_t SB_LOGGING_CHARACTERISTIC_UUID = 0x2B98;

namespace SbcPacket {
	enum {
		// GetAllBasicInfo = 0x0c,
		Model = 0x02,
		FwVersion = 0x03,
		Mode = 0x04,
		HeatOn = 0x06,
		Sensor1 = 0x07,
		Sensor2 = 0x08,
		Temperature = 0x09,
		Time = 0x0b,
		HdoEnabled = 0x14,
		LastHdoTime = 0x20,
		HdoLowTariff = 0x21,
		HdoInfo = 0x22,
		UidResponse = 0x33,
		Capacity = 0x3a,
		Name = 0x50,

		SetNormalTemperature = 0x0e,
		SetMode = 0x12,
		SetHdoEnabled = 0x1B,

		// Individual requests, replies come back as UidResponse
		ConsumptionStatsGetAll = 0x5d,

		RequestError = 0x34,
	};
}

// This is our ID, it's not related to the protocol.
// Ideally, we would use a counter and use that counter to match replies to requests.
static const uint8_t UID_CONSUMPTION = 0xcc;

static const char* modeStrings[] = {
	"STOP", "NORMAL", "HDO", "SMART", "SMARTHDO", "ANTIFROST", "NIGHT", "TEST"
};

SmartBoiler::SmartBoiler()
{
}

void SmartBoiler::setup()
{
	subscribe(root_topic_ + "set_temperature", &SmartBoiler::on_set_temperature);
	subscribe(root_topic_ + "set_mode", &SmartBoiler::on_set_mode);
	subscribe(root_topic_ + "set_hdo_enabled", &SmartBoiler::on_set_hdo_enabled);

	publish(root_topic_ + "boiler_online", "0", 0, true);

	esphome::mqtt::MQTTMessage lastWill = {
		.topic = root_topic_ + "boiler_online",
		.payload = "0",
	};

	esphome::mqtt::global_mqtt_client->set_last_will(std::move(lastWill));
}

void SmartBoiler::dump_config()
{
	ESP_LOGCONFIG(TAG, "SmartBoiler:");

	LOG_SENSOR("  ", "Device", temperature_sensor_1_sensor_);
	LOG_SENSOR("  ", "Device", temperature_sensor_2_sensor_);
	LOG_SENSOR("  ", "Consumption", consumption_sensor_);
	LOG_BINARY_SENSOR("  ", "Device", hdo_low_tariff_sensor_);
	LOG_SELECT("  ", "Mode", mode_select_);
	LOG_CLIMATE("  ", "Thermostat", thermostat_);
}

void SmartBoiler::send_to_boiler(uint8_t* frame, size_t length)
{
	auto status = esp_ble_gattc_write_char(this->parent_->gattc_if, this->parent_->conn_id, this->char_handle_,
								length, frame, ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);

	if (status)
		ESP_LOGW(TAG, "[%s] esp_ble_gattc_write_char failed, status=%d", this->parent_->address_str().c_str(), status);
}

void SmartBoiler::on_set_temperature(const std::string &payload)
{
	auto tempOpt = parse_number<int>(payload);
	if (!tempOpt)
	{
		ESP_LOGW(TAG, "Invalid set_temperature value: %s", payload.c_str());
		return;
	}

	int temp = *tempOpt;

	if (temp < 5 || temp > 74)
	{
		ESP_LOGW(TAG, "Invalid set temperature: %d", temp);
		return;
	}

	uint8_t frame[] = {
		SbcPacket::SetNormalTemperature, 0x00, 0x00, 0x00, uint8_t(temp & 0xff), 0x00, 0x00, 0x00
	};

	send_to_boiler(frame, sizeof(frame));
}

void SmartBoiler::on_set_temperature_int(int temp)
{
	if (temp < 5 || temp > 74)
	{
		ESP_LOGW(TAG, "Invalid set temperature: %d", temp);
		return;
	}

	uint8_t frame[] = {
		SbcPacket::SetNormalTemperature, 0x00, 0x00, 0x00, uint8_t(temp & 0xff), 0x00, 0x00, 0x00
	};

	send_to_boiler(frame, sizeof(frame));
}

void SmartBoiler::on_set_mode(const std::string &payload)
{
	uint8_t mode;
	auto modeStr = str_upper_case(payload);

	if (modeStr == "STOP")
		mode = 0;
	else if (modeStr == "NORMAL")
		mode = 1;
	else if (modeStr == "HDO")
		mode = 2;
	else if (modeStr == "SMART")
		mode = 3;
	else if (modeStr == "SMARTHDO")
		mode = 4;
	else if (modeStr == "ANTIFROST")
		mode = 5;
	else if (modeStr == "NIGHT")
		mode = 6;
	else
	{
		ESP_LOGW(TAG, "Unknown boiler mode set: %s", payload.c_str());
		return;
	}

	uint8_t frame[] = {
		SbcPacket::SetMode, 0x00, 0x00, 0x00, mode, 0x00, 0x00, 0x00
	};

	send_to_boiler(frame, sizeof(frame));
}

void SmartBoiler::on_set_hdo_enabled(const std::string &payload)
{
	auto hdoOpt = parse_number<int>(payload);
	if (!hdoOpt)
	{
		ESP_LOGW(TAG, "Invalid set_hdo_enabled value: %s", payload.c_str());
		return;
	}

	uint8_t frame[] = {
		SbcPacket::SetHdoEnabled, 0x00, 0x00, 0x00, uint8_t(*hdoOpt ? 1 : 0), 0x00, 0x00, 0x00
	};

	send_to_boiler(frame, sizeof(frame));
}

void SmartBoiler::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param)
{
	switch (event)
	{
		case ESP_GATTC_OPEN_EVT:
		{
			if (param->open.status == ESP_GATT_OK)
			{
				ESP_LOGI(TAG, "[%s] Connected",
							this->parent_->address_str().c_str());
			}
			break;
    	}
    	case ESP_GATTC_DISCONNECT_EVT:
		{
			ESP_LOGI(TAG, "[%s] Disconnected",
						this->parent_->address_str().c_str());

			publish(root_topic_ + "boiler_online", "0", 0, true);
			online_ = false;
			break;
		}
		case ESP_GATTC_SEARCH_CMPL_EVT:
		{
			auto *chr = this->parent_->get_characteristic(SB_MAIN_SERVICE_UUID, SB_MAIN_CHARACTERISTIC_UUID);
			if (chr == nullptr)
			{
				ESP_LOGE(TAG, "[%s] No main service found at device, not a SmartBoiler..?",
						this->parent_->address_str().c_str());
				break;
			}

			this->char_handle_ = chr->handle;

			chr = this->parent_->get_characteristic(SB_LOGGING_SERVICE_UUID, SB_LOGGING_CHARACTERISTIC_UUID);

			if (chr == nullptr)
			{
				ESP_LOGE(TAG, "[%s] No logging service found at device, not a SmartBoiler..?",
						this->parent_->address_str().c_str());
				break;
			}

			auto status = esp_ble_gattc_register_for_notify(this->parent()->gattc_if, this->parent()->remote_bda, chr->handle);
			if (status)
			{
				ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", status);
				break;
			}

			break;
		}
		case ESP_GATTC_REG_FOR_NOTIFY_EVT:
		{
			ESP_LOGI(TAG, "Send our UID");

			// This is the 1st command we need to send or the boiler disconnects and won't accept our commands
			uint8_t frame[] = {
				0x44, 0x00, 0x00, 0x00, 0x74, 0x39, 0x70, 0x61, 0x71, 0x6f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
			};

			send_to_boiler(frame, sizeof(frame));

			delay(50);
			request_value(SbcPacket::HdoEnabled);
			delay(50);
			request_value(SbcPacket::LastHdoTime);
			delay(50);
			request_value(SbcPacket::HdoInfo);
			delay(50);
			request_value(SbcPacket::HdoLowTariff);
			delay(50);

			// Using GetAllBasicInfo seems to cause strange issues (overload?), so let's ask separately
			request_value(SbcPacket::Model);
			delay(50);
			request_value(SbcPacket::FwVersion);
			delay(50);
			request_value(SbcPacket::Mode);
			delay(50);
			request_value(SbcPacket::HeatOn);
			delay(50);
			request_value(SbcPacket::Sensor1);
			delay(50);
			request_value(SbcPacket::Sensor2);
			delay(50);
			request_value(SbcPacket::Temperature);
			delay(50);
			request_value(SbcPacket::Capacity);
			delay(50);
			request_value(SbcPacket::Name);

			break;
		}
		case ESP_GATTC_NOTIFY_EVT:
		{
			handle_incoming(param->notify.value, param->notify.value_len);
			break;
		}
		default:
		{
			break;
		}
	}
}

void SmartBoiler::update()
{
	if (!online_)
		return;

	ESP_LOGD(TAG, "Requesting consumption");
	request_value(SbcPacket::ConsumptionStatsGetAll, UID_CONSUMPTION);
}

void SmartBoiler::request_value(uint8_t value, uint8_t uid)
{
	uint8_t frame[] = {
		value, 0x00, uid, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	send_to_boiler(frame, sizeof(frame));
}

void SmartBoiler::set_root_topic(const std::string &value)
{
	root_topic_ = value;

	if (!root_topic_.empty())
		root_topic_ += "/";
}

void SmartBoiler::handle_incoming(const uint8_t *data, uint16_t length)
{
	// First two bytes contain a decimal value from SbcPacket as a string
	uint8_t cmd = (data[0] - '0') * 10 + (data[1] - '0');
	std::string arg((const char*) data+2, length-2);

	if (!online_)
	{
		online_ = true;
		publish(root_topic_ + "boiler_online", "1", 0, true);
	}

	ESP_LOGI(TAG, "Received data from boiler: cmd=0x%x, %s, %d bytes", cmd, arg.c_str(), length - 2);

	switch (cmd)
	{
		case SbcPacket::FwVersion:
		{
			// Format: firmware;board revision;serial number
			auto firstSemicol = arg.find(';');

			if (firstSemicol != std::string::npos)
			{
				auto secondSemicol = arg.find(';', firstSemicol+1);

				if (secondSemicol != std::string::npos)
				{
					publish(root_topic_ + "fw_version", arg.substr(0, firstSemicol), 0, true);
					publish(root_topic_ + "board_rev", arg.substr(firstSemicol+1, secondSemicol-firstSemicol-1), 0, true);
					publish(root_topic_ + "serial", arg.substr(secondSemicol+1), 0, true);

					break;
				}
			}

			ESP_LOGW(TAG, "Bad FW info format: %s", arg.c_str());
			
			break;
		}

		case SbcPacket::Mode:
		{
			auto modeOpt = parse_number<int>(arg);
			if (!modeOpt)
			{
				ESP_LOGW(TAG, "Bad mode string from boiler: %s", arg.c_str());
				break;
			}

			int mode = *modeOpt;

			if (mode >= 0 && mode < sizeof(modeStrings) / sizeof(modeStrings[0]))
			{
				publish(root_topic_ + "mode", modeStrings[mode], 0, true);

				is_stopped_ = mode == 0;

				if (mode_select_)
					mode_select_->publish_state(modeStrings[mode]);
			}
			else
				ESP_LOGW(TAG, "Bad mode value from boiler: %d", mode);

			break;
		}

		case SbcPacket::Temperature:
		{
			auto tempOpt = parse_number<int>(arg);
			if (!tempOpt)
			{
				ESP_LOGW(TAG, "Bad set temp value from boiler: %s", arg.c_str());
				break;
			}

			if (thermostat_)
				thermostat_->publish_target_temp(*tempOpt);

			publish(root_topic_ + "temperature", to_string(*tempOpt), 0, true);
			break;
		}

		case SbcPacket::UidResponse: {
			switch (data[2])
			{
				case UID_CONSUMPTION:
				{
					uint32_t consumption;

					consumption = data[4];
					consumption |= uint32_t(data[5]) << 8;
					consumption |= uint32_t(data[6]) << 16;
					consumption |= uint32_t(data[7]) << 24;

					ESP_LOGD(TAG, "Consumption: %d Wh", consumption);

					if (consumption_sensor_)
						consumption_sensor_->publish_state(consumption);
					break;
				}
				default:
				{
					ESP_LOGW(TAG, "Unknown response UID: 0x%x", data[0]);
				}
			}
		}

		case SbcPacket::Time:
		{
			// format: weekday.time
			// weekday is 0-6
			publish(root_topic_ + "time", arg.substr(2));
			break;
		}

		case SbcPacket::Name:
		{
			publish(root_topic_ + "name", arg, 0, true);
			break;
		}

		case SbcPacket::HdoEnabled:
		{
			auto onoffOpt = parse_number<int>(arg);
			if (!onoffOpt)
			{
				ESP_LOGW(TAG, "Bad HDO state from boiler: %s", arg.c_str());
				break;
			}

			publish(root_topic_ + "hdo_enabled", *onoffOpt ? "1" : "0", 0, true);
			break;
		}

		case SbcPacket::LastHdoTime:
		{
			publish(root_topic_ + "last_hdo_time", arg.substr(2), 0, true);
			break;
		}

		case SbcPacket::HdoInfo:
		{
			publish(root_topic_ + "hdo_info", arg, 0, true);
			break;
		}

		case SbcPacket::Sensor1:
		{
			publish(root_topic_ + "sensor1", arg, 0, true);
			if (temperature_sensor_1_sensor_)
			{
				auto sensor1 = parse_number<float>(arg);
				if (sensor1)
					temperature_sensor_1_sensor_->publish_state(*sensor1);
			}
			break;
		}

		case SbcPacket::Sensor2:
		{
			publish(root_topic_ + "sensor2", arg, 0, true);

			auto sensor2 = parse_number<float>(arg);
			if (sensor2)
			{
				if (temperature_sensor_2_sensor_)
					temperature_sensor_2_sensor_->publish_state(*sensor2);
				if (thermostat_)
					thermostat_->publish_current_temp(*sensor2);
			}
			break;
		}

		case SbcPacket::Capacity:
		{
			publish(root_topic_ + "capacity", arg, 0, true);
			break;
		}

		case SbcPacket::Model:
		{
			publish(root_topic_ + "model", arg, 0, true);
			break;
		}

		case SbcPacket::HdoLowTariff:
		{
			auto onoffOpt = parse_number<int>(arg);
			if (!onoffOpt)
			{
				ESP_LOGW(TAG, "Bad HDO state from boiler: %s", arg.c_str());
				break;
			}

			if (hdo_low_tariff_sensor_)
			{
				bool hdo_low_tariff = !!*onoffOpt;
				hdo_low_tariff_sensor_->publish_state(hdo_low_tariff);
			}

			publish(root_topic_ + "hdo_low_tariff", *onoffOpt ? "1" : "0", 0, true);
			break;
		}

		case SbcPacket::HeatOn:
		{
			auto onoffOpt = parse_number<int>(arg);
			if (!onoffOpt)
			{
				ESP_LOGW(TAG, "Bad heat source state from boiler: %s", arg.c_str());
				break;
			}

			bool heat_on = !!*onoffOpt;
			if (heat_on_sensor_)
				heat_on_sensor_->publish_state(heat_on);
			if (thermostat_)
				thermostat_->publish_action(is_stopped_, heat_on);

			publish(root_topic_ + "heat_on", *onoffOpt ? "1" : "0", 0, true);
			break;
		}

		case SbcPacket::RequestError:
		{
			ESP_LOGW(TAG, "Boiler indicates that the last request has failed");
			break;
		}
	}
}

void SmartBoiler::set_mode(SmartBoilerModeSelect *s)
{
	mode_select_ = s;
	s->set_parent(this);
}

void SmartBoiler::set_thermostat(SmartBoilerThermostat *t)
{
	thermostat_ = t;
	t->set_parent(this);
}

void SmartBoilerModeSelect::control(const std::string &value)
{
	if (value.find("HDO") != std::string::npos)
		get_parent()->on_set_hdo_enabled("1");
	else
		get_parent()->on_set_hdo_enabled("0");

	get_parent()->on_set_mode(value);
}

SmartBoilerThermostat::SmartBoilerThermostat()
{
	this->mode = esphome::climate::CLIMATE_MODE_HEAT;
}

void SmartBoilerThermostat::control(const esphome::climate::ClimateCall &call)
{
	auto tt = call.get_target_temperature();
	if (tt)
	{
		int temp = *tt;
		get_parent()->on_set_temperature_int(temp);
	}
}

esphome::climate::ClimateTraits SmartBoilerThermostat::traits()
{
	esphome::climate::ClimateTraits rv;

	rv.set_visual_min_temperature(0);
	rv.set_visual_max_temperature(74);
	rv.set_visual_temperature_step(1);
	rv.set_supports_current_temperature(true);
	rv.set_supports_action(true);
	rv.set_supported_modes({ climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT });

	return rv;
}

void SmartBoilerThermostat::publish_target_temp(float temp)
{
	this->target_temperature = temp;
	publish_state();
}

void SmartBoilerThermostat::publish_current_temp(float temp)
{
	this->current_temperature = temp;
	publish_state();
}

void SmartBoilerThermostat::publish_action(bool stopped, bool heating)
{
	if (stopped)
		this->action = esphome::climate::CLIMATE_ACTION_OFF;
	else if (heating)
		this->action = esphome::climate::CLIMATE_ACTION_HEATING;
	else
		this->action = esphome::climate::CLIMATE_ACTION_IDLE;
	publish_state();
}

}
}
