#include "smartboiler.h"
#include "esphome/core/application.h"
#include "esphome/components/md5/md5.h"

#define UUID_LENGTH 6

static const char *const TAG = "smartboiler";

namespace esphome {
namespace sb {

// Requests are written to this characteristic
static const uint16_t SB_MAIN_SERVICE_UUID = 0x1899;
static const uint16_t SB_MAIN_CHARACTERISTIC_UUID = 0x2B99;

// Responses arrive through this characteristic
static const uint16_t SB_LOGGING_SERVICE_UUID = 0x1898;
static const uint16_t SB_LOGGING_CHARACTERISTIC_UUID = 0x2B98;
static const uint16_t CLIENT_CHARACTERISTIC_CONFIG_DESCRIPTOR_UUID = 0x2902;

static const int COMMAND_DELAY = 100;

void SmartBoiler::restore_state_() {
  SavedSmartBoilerSettings recovered{};
  this->pref_ = global_preferences->make_preference<SavedSmartBoilerSettings>(this->thermostat_->get_object_id_hash());
  bool restored = this->pref_.load(&recovered);
  if (restored) {
    this->uid_ = recovered.uid;
    ESP_LOGD(TAG, "using stored UID: %s", this->uid_.c_str());
  }
}

void SmartBoiler::save_state_() {
  SavedSmartBoilerSettings state{};
  strcpy(state.uid, this->uid_.c_str());
  this->pref_.save(&state);
}

void SmartBoiler::setup() {
  ESP_LOGD(TAG, "Setup()");
  this->restore_state_();
  if (this->uid_.size() == 0) {
    this->uid_ = this->generateUUID();
    ESP_LOGD(TAG, "generated new UUID: %s", this->uid_.c_str());
    this->save_state_();
  }
  this->state_txt_->publish_state(this->state_to_string(this->state_));
}

void SmartBoiler::dump_config() {
  ESP_LOGCONFIG(TAG, "SmartBoiler:");
  LOG_SENSOR("  ", "Temp1", temperature_sensor_1_sensor_);
  LOG_SENSOR("  ", "Temp2", temperature_sensor_2_sensor_);
  LOG_SENSOR("  ", "Consumption", consumption_sensor_);
  LOG_BINARY_SENSOR("  ", "HDO", hdo_low_tariff_sensor_);
  LOG_SELECT("  ", "Mode", mode_select_);
  LOG_CLIMATE("  ", "Thermostat", thermostat_);
  LOG_NUMBER("  ", "Pairing PIN", mPin_);
  LOG_TEXT_SENSOR("  ", "Version", version_);
  LOG_TEXT_SENSOR("  ", "State", state_txt_);
}

void SmartBoiler::loop() { this->process_command_queue_(); }

void SmartBoiler::send_to_boiler(SBProtocolRequest request) {
  this->last_command_timestamp_ = millis();
  ESP_LOGD(TAG, "Sending: REQ: %d DATA=[%s]", request.mRqType, format_hex_pretty(request.mData).c_str());
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(),
                                         this->char_handle_, request.mData.size(), request.mData.data(),
                                         ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);

  if (status)
    ESP_LOGW(TAG, "[%s] esp_ble_gattc_write_char failed, status=%d", this->parent_->address_str().c_str(), status);
}

void SmartBoiler::on_set_temperature(uint8_t temp) {
  if (temp < MIN_TEMP || temp > MAX_TEMP) {
    ESP_LOGW(TAG, "Invalid set temperature: %d", temp);
    return;
  }
  auto cmd = SBProtocolRequest(SBC_PACKET_HOME_SETNORMALTEMPERATURE, this->mPacketUid++);
  cmd.write_le((uint32_t) temp);
  this->enqueue_command_(cmd);
}

void SmartBoiler::on_set_mode(const std::string &payload) {
  auto mode = this->convert_action_to_mode(payload);
  auto cmd = SBProtocolRequest(SBC_PACKET_HOME_SETMODE, this->mPacketUid++);
  cmd.write_le(uint32_t(mode));
  this->enqueue_command_(cmd);
}

void SmartBoiler::on_set_hdo_enabled(const std::string &payload) {
  auto hdoOpt = parse_number<int>(payload);
  if (!hdoOpt) {
    ESP_LOGW(TAG, "Invalid set_hdo_enabled value: %s", payload.c_str());
    return;
  }
  auto cmd = SBProtocolRequest(SBC_PACKET_HDO_SET_ONOFF, this->mPacketUid++);
  cmd.write_le(uint32_t(*hdoOpt ? 1 : 0));
  this->enqueue_command_(cmd);
}

void SmartBoiler::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                      esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        ESP_LOGI(TAG, "[%s] Connected", this->parent_->address_str().c_str());
      }
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGI(TAG, "[%s] Disconnected", this->parent_->address_str().c_str());
      this->set_state(ConnectionState::DISCONNECTED);
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto chr = this->parent_->get_characteristic(SB_MAIN_SERVICE_UUID, SB_MAIN_CHARACTERISTIC_UUID);
      if (chr == nullptr) {
        ESP_LOGE(TAG, "[%s] No main service found at device, not a SmartBoiler..?",
                 this->parent_->address_str().c_str());
        this->parent_->disconnect();
        break;
      }

      this->char_handle_ = chr->handle;

      chr = this->parent_->get_characteristic(SB_LOGGING_SERVICE_UUID, SB_LOGGING_CHARACTERISTIC_UUID);

      if (chr == nullptr) {
        ESP_LOGE(TAG, "[%s] No logging service found at device, not a SmartBoiler..?",
                 this->parent_->address_str().c_str());
        this->parent_->disconnect();
        break;
      }
      auto status = esp_ble_gattc_register_for_notify(this->parent()->get_gattc_if(), this->parent()->get_remote_bda(),
                                                      chr->handle);
      if (status) {
        ESP_LOGW(TAG, "esp_ble_gattc_register_for_notify failed, status=%d", status);
        break;
      }

      break;
    }
    case ESP_GATTC_REG_FOR_NOTIFY_EVT: {
      this->authenticate();
      break;
    }
    case ESP_GATTC_NOTIFY_EVT: {
      handle_incoming(param->notify.value, param->notify.value_len);
      break;
    }
    default:
      break;
  }
}

void SmartBoiler::authenticate() {
  ESP_LOGD(TAG, "Sending authentication request.");
  auto cmd = SBProtocolRequest(SBPacket::SBC_PACKET_RQ_GLOBAL_MAC);
  cmd.writeString(this->uid_);
  this->enqueue_command_(cmd);
  this->set_state(ConnectionState::AUTHENTICATING);
}

void SmartBoiler::getInitData() {
  ESP_LOGD(TAG, "Requesting initial data from water heater");
  this->request_value(SBPacket::SBC_PACKET_HOME_MODE);
  this->request_value(SBPacket::SBC_PACKET_HOME_BOILERMODEL);
  this->request_value(SBPacket::SBC_PACKET_HOME_CAPACITY);
  this->request_value(SBPacket::SBC_PACKET_HOME_BOILERNAME);
  this->request_value(SBPacket::SBC_PACKET_HOME_TEMPERATURE);
  this->request_value(SBPacket::SBC_PACKET_HOME_SENSOR1);
  this->request_value(SBPacket::SBC_PACKET_HOME_SENSOR2);
  this->request_value(SBPacket::SBC_PACKET_HOME_HSRCSTATE);
  this->request_value(SBPacket::SBC_PACKET_HDO_ONOFF);
}

void SmartBoiler::update() {
  if (this->state_ == ConnectionState::CONNECTED) {
    ESP_LOGD(TAG, "Requesting consumption");
    auto cmd = SBProtocolRequest(SBC_PACKET_STATISTICS_GETALL, this->mPacketUid++);
    this->enqueue_command_(cmd);
  }
}

void SmartBoiler::request_value(SBPacket sensor, uint16_t uid) {
  this->enqueue_command_(SBProtocolRequest(sensor, uid));
}

void SmartBoiler::handle_incoming(const uint8_t *value, uint16_t value_len) {
  auto result = SBProtocolResult(value, value_len);

  ESP_LOGD(TAG, "Received: REQ: %d DATA=[%s]", result.mRqType, format_hex_pretty(value, value_len).c_str());

  switch (result.mRqType) {
    case SBPacket::SBC_PACKET_GLOBAL_PAIRPIN: {
      ESP_LOGD(TAG, "PIN pairing required.");
      // water heater requires pairing of this client via PIN
      this->set_state(ConnectionState::NEED_PIN);
      break;
    }
    case SBPacket::SBC_PACKET_GLOBAL_CONFIRMUID: {
      // all setting commands are sent with unique packet UID and confirmation from BT
      // server is expected. Most of them contain no additional data, with exception of
      // SBC_PACKET_STATISTICS_GETALL
      ESP_LOGD(TAG, "Received confirmation for packet with UID: %0X", result.mUid);
      ESP_LOGD(TAG, "mByteData: DATA=[%s]", format_hex_pretty(result.mByteData).c_str());

      // find original request in queue of sent packets
      auto originalRequest = std::find_if(this->sent_queue_.begin(), this->sent_queue_.end(),
                                          [&](const SBProtocolRequest& req) { return req.mUid == result.mUid; });
      if (originalRequest != this->sent_queue_.end()) {
        ESP_LOGD(TAG, "original request was: %d", (*originalRequest).mRqType);
        // remove the sent request from the queue as it was sucessfully accepted
        this->sent_queue_.erase(originalRequest);

        // handle some special confirm packets
        switch ((*originalRequest).mRqType) {
          case SBPacket::SBC_PACKET_STATISTICS_GETALL: {
            uint32_t consumption = result.load_uint32_le(0);
            uint32_t timestamp = result.load_uint32_le(4);
            if (this->consumption_sensor_)
              this->consumption_sensor_->publish_state((float) consumption / 1000);
            break;
          }
          default:
            break;
        }
      }
      break;
    }
    case SBPacket::SBC_PACKET_GLOBAL_DEVICEBONDED: {
      ESP_LOGI(TAG, "Device is already paired with the water heater.");
      this->set_state(ConnectionState::CONNECTED);
      this->getInitData();
      break;
    }
    case SBPacket::SBC_PACKET_GLOBAL_PINRESULT: {
      auto pinResult = parse_number<int>(result.mString);
      if (pinResult.has_value() && pinResult.value() == 1) {
        ESP_LOGI(TAG, "PIN is correct.");
        this->set_state(ConnectionState::CONNECTED);
        this->getInitData();
      } else {
        ESP_LOGW(TAG, "Wrong PIN provided, water heater response: %s", result.mString.c_str());
      }
      break;
    }

    case SBPacket::SBC_PACKET_HOME_FWVERSION: {
      // Format: firmware;board revision;serial number
      auto firstSemicol = result.mString.find(';');
      if (firstSemicol != std::string::npos) {
        auto secondSemicol = result.mString.find(';', firstSemicol + 1);
        if (secondSemicol != std::string::npos) {
          auto fwVersion = result.mString.substr(0, firstSemicol);
          auto boardRev = result.mString.substr(firstSemicol + 1, secondSemicol - firstSemicol - 1);
          auto serial = result.mString.substr(secondSemicol + 1);
          char buffer[100];
          sprintf(buffer, "fw:%s, board: %s, S/N: %s", fwVersion.c_str(), boardRev.c_str(), serial.c_str());
          std::string version(buffer);
          this->version_->publish_state(version);
          break;
        }
      }
      ESP_LOGW(TAG, "Bad FW info format: %s", result.mString.c_str());
      break;
    }

    case SBPacket::SBC_PACKET_HOME_MODE: {
      auto modeOpt = parse_number<int>(result.mString);
      if (!modeOpt.has_value()) {
        ESP_LOGW(TAG, "Bad mode string from water heater: %s", result.mString.c_str());
        break;
      }
      auto mode = modeOpt.value();
      auto modeAsString = convert_mode_to_action(mode);
      if (!modeAsString.empty()) {
        if (mode_select_)
          mode_select_->publish_state(modeAsString);
      } else
        ESP_LOGW(TAG, "Bad mode value from water heater: %d", mode);
      break;
    }
    case SBPacket::SBC_PACKET_HOME_TEMPERATURE: {
      auto tempOpt = parse_number<float>(result.mString);
      if (tempOpt.has_value()) {
        this->thermostat_->publish_target_temp(*tempOpt);
      }
      break;
    }
    case SBPacket::SBC_PACKET_HOME_SENSOR1: {
      auto tempOpt = parse_number<float>(result.mString);
      if (tempOpt.has_value()) {
        this->temperature_sensor_1_sensor_->publish_state(*tempOpt);
      }
      break;
    }
    case SBPacket::SBC_PACKET_HOME_SENSOR2: {
      auto tempOpt = parse_number<float>(result.mString);
      if (tempOpt.has_value()) {
        this->temperature_sensor_2_sensor_->publish_state(*tempOpt);
        if (this->thermostat_)
          this->thermostat_->publish_current_temp(*tempOpt);
      }
      break;
    }
    case SBPacket::SBC_PACKET_HOME_HSRCSTATE: {
      auto heatOpt = parse_number<int>(result.mString);
      auto is_heating = *heatOpt == 1;
      this->heat_on_sensor_->publish_state(is_heating);
      if (thermostat_)
        this->thermostat_->publish_action(is_heating);
      break;
    }
    case SBPacket::SBC_PACKET_HDO_ONOFF: {
      auto hdoOpt = parse_number<int>(result.mString);
      this->isHdoEnabled = hdoOpt.value() == 1;
      this->hdo_low_tariff_sensor_->publish_state(this->isHdoEnabled);
      ESP_LOGI(TAG, "Internal HDO decoder is %s.", this->isHdoEnabled ? "enabled" : "disabled");
      break;
    }
    case SBPacket::SBC_PACKET_HOME_BOILERNAME: {
      this->name_->publish_state(result.mString);
      break;
    }
    case SBPacket::SBC_PACKET_HOME_ERROR: {
      ESP_LOGW(TAG, "water heater indicates that the last request has failed");
      break;
    }
    case SBPacket::SBC_PACKET_HOME_TIME: {
      // time is in format D.HH:MM:SS
      // first byte is day
      auto day = result.mString[0] - '0';
      // the rest is a string
      auto time = result.mString.substr(2,8);
      char buffer[30];
      sprintf(buffer, "%s, %s", this->day_to_string(day), time.c_str());
      ESP_LOGD(TAG, "Water heater internal time: %s", buffer);
      break;
    }
    default:
      break;
  }
}

void SmartBoilerModeSelect::control(const std::string &value) { get_parent()->on_set_mode(value); }

void SmartBoilerThermostat::control(const esphome::climate::ClimateCall &call) {
  if (call.get_target_temperature().has_value()) {
    float tt = *call.get_target_temperature();
    uint8_t target_temp = (uint8_t) tt;
    get_parent()->on_set_temperature(target_temp);
  }
}

esphome::climate::ClimateTraits SmartBoilerThermostat::traits() {
  esphome::climate::ClimateTraits rv;

  rv.set_visual_min_temperature(MIN_TEMP);
  rv.set_visual_max_temperature(MAX_TEMP);
  rv.set_visual_temperature_step(1);
  rv.set_supports_current_temperature(true);
  rv.set_supports_action(true);
  rv.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});

  return rv;
}

void SmartBoilerThermostat::publish_target_temp(float temp) {
  this->target_temperature = temp;
  this->publish_state();
}

void SmartBoilerThermostat::publish_current_temp(float temp) {
  this->current_temperature = temp;
  this->publish_state();
}

void SmartBoilerThermostat::publish_action(bool heating) {
  if (get_parent()->mode_select_->state == "STOP")
    this->action = esphome::climate::CLIMATE_ACTION_OFF;
  else if (heating)
    this->action = esphome::climate::CLIMATE_ACTION_HEATING;
  else
    this->action = esphome::climate::CLIMATE_ACTION_IDLE;
  this->publish_state();
}

void SmartBoiler::enqueue_command_(const SBProtocolRequest &command) {
  this->command_queue_.push_back(command);
  this->process_command_queue_();
}

void SmartBoiler::process_command_queue_() {
  uint32_t now = millis();
  uint32_t cmdDelay = now - this->last_command_timestamp_;

  if (cmdDelay > COMMAND_DELAY && !this->command_queue_.empty()) {
    auto nextCmd = this->command_queue_.front();
    this->send_to_boiler(nextCmd);
    if (nextCmd.mUid) {
      this->sent_queue_.push_back(nextCmd);
    }
    this->command_queue_.erase(this->command_queue_.begin());
    ESP_LOGD(TAG, "Queue status - QUEUE=[%d], SENT_QUEUE=[%d]", this->command_queue_.size(), this->sent_queue_.size());
  }
}

/**
 * Generate random UUID for this device.
 */
std::string SmartBoiler::generateUUID() {
  char sbuf[16];
  md5::MD5Digest md5{};
  md5.init();
  sprintf(sbuf, "%08X", random_uint32());
  md5.add(sbuf, 8);
  md5.calculate();
  md5.get_hex(sbuf);
  ESP_LOGV(TAG, "Auth: Nonce is %s", sbuf);
  std::string s(sbuf);
  return s.substr(0, 6);
}

void SmartBoiler::send_pin(uint32_t pin) {
  ESP_LOGD(TAG, "Sending PIN to water heater.");
  auto cmd = SBProtocolRequest(SBC_PACKET_GLOBAL_PAIRPIN, this->mPacketUid++);
  cmd.write_le(pin);
  this->enqueue_command_(cmd);
}

const char *SmartBoiler::state_to_string(ConnectionState state) {
  switch (state) {
    case ConnectionState::CONNECTED:
      return "Connected";
    case ConnectionState::NEED_PIN:
      return "Require PIN";
    case ConnectionState::AUTHENTICATING:
      return "Authenticating";
    case ConnectionState::DISCONNECTED:
      return "Disconnected";
    default:
      return "Unknown";
  }
}

const char *SmartBoiler::day_to_string(uint8_t day) {
  switch (day) {
    case 0:
      return "Monday";
    case 1:
      return "Tuesday";
    case 2:
      return "Wednesday";
    case 3:
      return "Thursday";
    case 4:
      return "Friday";
    case 5:
      return "Saturday";
    case 6:
      return "Sunday";
    default:
      return "Unknown";
  }
}

void SmartBoiler::set_state(ConnectionState newState) {
  this->state_ = newState;
  this->state_txt_->publish_state(this->state_to_string(newState));
}

void SmartBoilerPinInput::control(const float value) {
  if (get_parent()->state_ == ConnectionState::NEED_PIN) {
    uint32_t pin = floor(value);
    get_parent()->send_pin(pin);
  } else {
    ESP_LOGW(TAG, "Device is not in pairing mode, PIN change is ignored.");
  }
}

/**
 * Convert string value from HA to code used by water heaters.
 * Modes SMART and MANUAL are converted to SMART_HDO/HDO in case
 * the water heater is configured to use HDO internal decoder.
 */
uint8_t SmartBoiler::convert_action_to_mode(const std::string &payload) {
  uint8_t mode = Mode::STOP;
  auto modeStr = str_upper_case(payload);

  if (modeStr == MODE_ANTIFREEZE)
    mode = Mode::ANTIFREEZE;
  else if (modeStr == MODE_SMART)
    mode = this->isHdoEnabled ? Mode::SMART_HDO : Mode::SMART;
  else if (modeStr == MODE_PROG)
    mode = Mode::PROG;
  else if (modeStr == MODE_MANUAL)
    mode = this->isHdoEnabled ? Mode::HDO : Mode::MANUAL;
  else {
    ESP_LOGW(TAG, "Unknown water heater mode: %s", payload.c_str());
  }
  return mode;
}

std::string SmartBoiler::convert_mode_to_action(const uint8_t mode) {
  switch (mode) {
    case Mode::ANTIFREEZE:
      return MODE_ANTIFREEZE;
    case Mode::SMART:
      return MODE_SMART;
    case Mode::SMART_HDO:
      return MODE_SMART;
    case Mode::PROG:
      return MODE_PROG;
    case Mode::MANUAL:
      return MODE_MANUAL;
    case Mode::HDO:
      return MODE_MANUAL;
    default:
      ESP_LOGW(TAG, "Unknown water heater mode: %d", mode);
      break;
  }
  return MODE_ANTIFREEZE;
}

}  // namespace sb
}  // namespace esphome
