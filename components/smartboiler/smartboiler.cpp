#include "smartboiler.h"
#include "esphome/core/application.h"
#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstring>
#include <string>

#ifdef USE_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

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
  // UID is stored under a key derived from the water heater MAC address, so it does not depend on entity names
  uint32_t key = fnv1_hash(std::string("smartboiler_") + this->parent_->address_str());
  this->pref_ = global_preferences->make_preference<SavedSmartBoilerSettings>(key);
  bool restored = this->pref_.load(&recovered);
  if (!restored && this->thermostat_ != nullptr) {
    // migration: older versions stored UID under the thermostat object id hash
    auto legacy = global_preferences->make_preference<SavedSmartBoilerSettings>(this->thermostat_->get_object_id_hash());
    restored = legacy.load(&recovered);
    if (restored) {
      ESP_LOGI(TAG, "Migrating stored UID to the new storage key");
      this->pref_.save(&recovered);
    }
  }
  if (restored) {
    this->uid_ = std::string(recovered.uid, strnlen(recovered.uid, sizeof(recovered.uid)));
    ESP_LOGD(TAG, "using stored UID: %s", this->uid_.c_str());
  }
}

void SmartBoiler::save_state_() {
  SavedSmartBoilerSettings state{};
  strncpy(state.uid, this->uid_.c_str(), sizeof(state.uid));
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
  if (this->state_txt_)
    this->state_txt_->publish_state(this->state_to_string(this->state_));
  this->publish_monitoring_();
}

void SmartBoiler::publish_monitoring_() {
  if (this->monitoring_switch_)
    this->monitoring_switch_->publish_state(this->parent_->enabled);
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
  LOG_TEXT_SENSOR("  ", "Name", name_);
  LOG_TEXT_SENSOR("  ", "Model", model_txt_);
  LOG_TEXT_SENSOR("  ", "Error", error_txt_);
  LOG_BINARY_SENSOR("  ", "Heating", heat_on_sensor_);
  LOG_BINARY_SENSOR("  ", "Low tariff", low_tariff_sensor_);
  LOG_BINARY_SENSOR("  ", "Problem", problem_sensor_);
  LOG_SENSOR("  ", "Anode voltage", anode_voltage_sensor_);
  LOG_SENSOR("  ", "Capacity", capacity_sensor_);
  LOG_SENSOR("  ", "PROG temperature", prog_temperature_sensor_);
  LOG_SWITCH("  ", "HDO", hdo_switch_);
  LOG_SWITCH("  ", "Anti-legionella", antilegionella_switch_);
  LOG_SWITCH("  ", "Monitoring", monitoring_switch_);
  LOG_NUMBER("  ", "PROG low temperature", prog_temp_low_number_);
  LOG_NUMBER("  ", "PROG high temperature", prog_temp_high_number_);
  LOG_NUMBER("  ", "Holiday start", holiday_start_number_);
  LOG_NUMBER("  ", "Holiday length", holiday_length_number_);
}

void SmartBoiler::loop() {
  uint32_t now = millis();
  if (this->state_ == ConnectionState::AUTHENTICATING && now - this->auth_sent_at_ > AUTH_RETRY_INTERVAL) {
    ESP_LOGD(TAG, "No response to authentication request, retrying.");
    this->authenticate();
  }
  if (this->state_ == ConnectionState::CONNECTED && this->low_tariff_sensor_ != nullptr &&
      now - this->last_low_tariff_poll_ > LOW_TARIFF_POLL_INTERVAL) {
    this->last_low_tariff_poll_ = now;
    this->request_value(SBPacket::SBC_PACKET_HDO_LESSEXPTARIFFAVAILABLENOW);
  }
  if (now - this->last_sent_queue_check_ > SENT_QUEUE_CHECK_INTERVAL) {
    this->last_sent_queue_check_ = now;
    this->check_sent_queue_();
  }
  this->process_command_queue_();
}

uint16_t SmartBoiler::next_packet_uid_() {
  // UID 0 means "no answer expected". The original app reads only the low byte of UID
  // from confirmation packets, so keep UIDs in range 1..255.
  this->mPacketUid = (this->mPacketUid % 255) + 1;
  return this->mPacketUid;
}

void SmartBoiler::check_sent_queue_() {
  uint32_t now = millis();
  for (auto it = this->sent_queue_.begin(); it != this->sent_queue_.end();) {
    if (now - it->mFirstSent > PACKET_EXPIRATION) {
      ESP_LOGW(TAG, "Request %d (UID %d) was not confirmed by water heater, giving up.", it->mRqType, it->mUid);
      it = this->sent_queue_.erase(it);
    } else if (now - it->mLastSent > PACKET_RESEND_INTERVAL && it->mRqType != SBC_PACKET_HOME_SETTIME) {
      // time adjustment is relative, re-sending it could shift the clock twice
      ESP_LOGD(TAG, "Request %d (UID %d) not confirmed yet, re-sending.", it->mRqType, it->mUid);
      this->command_queue_.push_back(*it);
      it = this->sent_queue_.erase(it);
    } else {
      ++it;
    }
  }
}

void SmartBoiler::clear_queues_() {
  this->command_queue_.clear();
  this->sent_queue_.clear();
}

bool SmartBoiler::is_time_adjustment_pending_() {
  auto is_settime = [](const SBProtocolRequest &req) { return req.mRqType == SBC_PACKET_HOME_SETTIME; };
  return std::any_of(this->command_queue_.begin(), this->command_queue_.end(), is_settime) ||
         std::any_of(this->sent_queue_.begin(), this->sent_queue_.end(), is_settime);
}

void SmartBoiler::send_to_boiler(SBProtocolRequest request) {
  this->last_command_timestamp_ = millis();
  ESP_LOGD(TAG, "Sending: REQ: %d DATA=[%s]", request.mRqType, format_hex_pretty(request.mData).c_str());
  auto status = esp_ble_gattc_write_char(this->parent_->get_gattc_if(), this->parent_->get_conn_id(),
                                         this->char_handle_, request.mData.size(), request.mData.data(),
                                         ESP_GATT_WRITE_TYPE_NO_RSP, ESP_GATT_AUTH_REQ_NONE);

  if (status)
    ESP_LOGW(TAG, "[%s] esp_ble_gattc_write_char failed, status=%d", this->parent_->address_str(), status);
}

void SmartBoiler::on_set_temperature(uint8_t temp) {
  if (temp < MIN_TEMP || temp > MAX_TEMP) {
    ESP_LOGW(TAG, "Invalid set temperature: %d", temp);
    return;
  }
  auto cmd = SBProtocolRequest(SBC_PACKET_HOME_SETNORMALTEMPERATURE, this->next_packet_uid_());
  cmd.write_le((uint32_t) temp);
  this->enqueue_command_(cmd);
}

void SmartBoiler::on_set_mode(const std::string &payload) {
  auto mode = this->convert_action_to_mode(payload);
  if (!mode.has_value()) {
    // revert the select in HA to the real mode of the water heater
    if (this->current_mode_ >= 0)
      this->publish_mode_(this->current_mode_);
    return;
  }
  auto cmd = SBProtocolRequest(SBC_PACKET_HOME_SETMODE, this->next_packet_uid_());
  cmd.write_le(uint32_t(*mode));
  this->enqueue_command_(cmd);
}

void SmartBoiler::on_switch(SwitchType type, bool state) {
  switch (type) {
    case SwitchType::HDO: {
      auto cmd = SBProtocolRequest(SBC_PACKET_HDO_SET_ONOFF, this->next_packet_uid_());
      cmd.write_le(uint32_t(state ? 1 : 0));
      this->enqueue_command_(cmd);
      break;
    }
    case SwitchType::ANTILEGIONELLA: {
      if (this->fw_version_ > 0 && this->fw_version_ < MIN_FW_ANTILEGIONELLA) {
        ESP_LOGW(TAG, "Anti-legionella requires water heater firmware %.2f or newer", MIN_FW_ANTILEGIONELLA);
        this->refresh_after_(SBC_PACKET_HOME_ANTILEGIO);
        return;
      }
      // same payload as original app: [1 = set, state]
      auto cmd = SBProtocolRequest(SBC_PACKET_HOME_ANTILEGIO, this->next_packet_uid_());
      cmd.write_le(uint8_t(1));
      cmd.write_le(uint8_t(state ? 1 : 0));
      this->enqueue_command_(cmd);
      break;
    }
    case SwitchType::MONITORING:
      // handled directly in SmartBoilerSwitch::write_state
      break;
  }
}

void SmartBoiler::on_number(NumberType type, float value) {
  int v = (int) lroundf(value);
  switch (type) {
    case NumberType::PROG_TEMP_LOW:
    case NumberType::PROG_TEMP_HIGH: {
      if (this->prog_temp_low_ < 0 || this->prog_temp_high_ < 0) {
        ESP_LOGW(TAG, "PROG temperatures not known yet, try again later.");
        return;
      }
      int low = type == NumberType::PROG_TEMP_LOW ? v : this->prog_temp_low_;
      int high = type == NumberType::PROG_TEMP_HIGH ? v : this->prog_temp_high_;
      if (low >= high) {
        ESP_LOGW(TAG, "PROG low temperature (%d) must be lower than high temperature (%d)", low, high);
        this->refresh_after_(SBC_PACKET_NIGHT_SAVEMINMAX);
        return;
      }
      this->send_prog_temperatures_(low, high);
      break;
    }
    case NumberType::HOLIDAY_START:
    case NumberType::HOLIDAY_LENGTH: {
      int start = type == NumberType::HOLIDAY_START ? v : std::max(this->holiday_start_, 0);
      int length = type == NumberType::HOLIDAY_LENGTH ? v : std::max(this->holiday_length_, 0);
      if (type == NumberType::HOLIDAY_START && length == 0) {
        // keep the start value until the length is set as well
        this->holiday_start_ = start;
        if (this->holiday_start_number_)
          this->holiday_start_number_->publish_state(start);
        ESP_LOGI(TAG, "Holiday start set to %d days, set holiday length to activate it.", start);
        return;
      }
      this->send_holiday_(start, length);
      break;
    }
  }
}

void SmartBoiler::send_prog_temperatures_(int low, int high) {
  ESP_LOGI(TAG, "Setting PROG temperatures: low %d, high %d", low, high);
  auto cmd = SBProtocolRequest(SBC_PACKET_NIGHT_SAVEMINMAX, this->next_packet_uid_());
  cmd.write_le(uint32_t(low));
  cmd.write_le(uint32_t(high));
  this->enqueue_command_(cmd);
}

void SmartBoiler::send_holiday_(int start, int length) {
  if (length <= 0) {
    ESP_LOGI(TAG, "Cancelling holiday");
    this->enqueue_command_(SBProtocolRequest(SBC_PACKET_HOLIDAY_DELETE, this->next_packet_uid_()));
    return;
  }
  ESP_LOGI(TAG, "Setting holiday: starts in %d days, lasts %d days", start, length);
  auto cmd = SBProtocolRequest(SBC_PACKET_HOLIDAY_SET, this->next_packet_uid_());
  cmd.write_le(uint32_t(start));
  cmd.write_le(uint32_t(length));
  this->enqueue_command_(cmd);
}

void SmartBoiler::on_reset_energy() {
  // original app sends current unix time - it's reported back as the "consumption since" timestamp
  uint32_t now = 0;
#ifdef USE_TIME
  if (this->time_clock_ != nullptr && this->time_clock_->now().is_valid())
    now = this->time_clock_->now().timestamp;
#endif
  ESP_LOGI(TAG, "Resetting energy consumption counter");
  auto cmd = SBProtocolRequest(SBC_PACKET_POWERCONS_RESET, this->next_packet_uid_());
  cmd.write_le(now);
  this->enqueue_command_(cmd);
}

/**
 * Request fresh values from water heater after a setting request was confirmed
 * or rejected, so HA always shows the real state.
 */
void SmartBoiler::refresh_after_(SBPacket request) {
  if (this->state_ != ConnectionState::CONNECTED)
    return;
  switch (request) {
    case SBC_PACKET_HOME_SETNORMALTEMPERATURE:
      this->request_value(SBC_PACKET_HOME_TEMPERATURE);
      break;
    case SBC_PACKET_HOME_SETMODE:
      this->request_value(SBC_PACKET_HOME_MODE);
      break;
    case SBC_PACKET_HDO_SET_ONOFF:
      this->request_value(SBC_PACKET_HDO_ONOFF);
      this->request_value(SBC_PACKET_HOME_MODE);
      break;
    case SBC_PACKET_HOME_ANTILEGIO:
      this->request_value(SBC_PACKET_HOME_ANTILEGIO);
      break;
    case SBC_PACKET_NIGHT_SAVEMINMAX:
      this->request_value(SBC_PACKET_HOME_TEMPNIGHTLOW);
      this->request_value(SBC_PACKET_HOME_TEMPNIGHT);
      this->request_value(SBC_PACKET_HOME_TEMPNIGHTCURR);
      break;
    case SBC_PACKET_HOLIDAY_SET:
    case SBC_PACKET_HOLIDAY_DELETE:
      this->request_value(SBC_PACKET_HOLIDAY_GET);
      this->request_value(SBC_PACKET_HOME_MODE);
      break;
    case SBC_PACKET_POWERCONS_RESET:
      this->enqueue_command_(SBProtocolRequest(SBC_PACKET_STATISTICS_GETALL, this->next_packet_uid_()));
      break;
    default:
      break;
  }
}

static const char *const ERROR_TEXTS[] = {
    "Excessive wear of anode",                        // Er01
    "Upper temperature sensor failure",               // Er02
    "Lower temperature sensor failure",               // Er03
    "Internal memory content error",                  // Er04
    "Failure of both temperature sensors",            // Er05
    "External flash memory content error",            // Er06
    "Bluetooth communication error",                  // Er07
    "High internal temperature of the electronics",   // Er08
    "High water temperature in the heater",           // Er09
};

void SmartBoiler::publish_error_(uint32_t mask) {
  if (this->problem_sensor_)
    this->problem_sensor_->publish_state(mask != 0);
  std::string text;
  for (uint8_t bit = 0; bit < sizeof(ERROR_TEXTS) / sizeof(ERROR_TEXTS[0]); bit++) {
    if (mask & (1u << bit)) {
      char code[8];
      snprintf(code, sizeof(code), "Er%02d: ", bit + 1);
      if (!text.empty())
        text += "; ";
      text += code;
      text += ERROR_TEXTS[bit];
    }
  }
  if (mask != 0 && text.empty())
    text = "Unknown error " + to_string(mask);
  if (mask != 0)
    ESP_LOGW(TAG, "Water heater reports error: %s", text.c_str());
  if (this->error_txt_)
    this->error_txt_->publish_state(mask == 0 ? "OK" : text);
}

void SmartBoiler::parse_holiday_(const std::string &payload) {
  // two 8 digit decimal numbers: days to begin, length (0/0 = no holiday)
  if (payload.size() < 16) {
    ESP_LOGW(TAG, "Bad holiday format from water heater: %s", payload.c_str());
    return;
  }
  auto start = parse_number<int>(payload.substr(0, 8));
  auto length = parse_number<int>(payload.substr(8, 8));
  if (!start.has_value() || !length.has_value()) {
    ESP_LOGW(TAG, "Bad holiday format from water heater: %s", payload.c_str());
    return;
  }
  this->holiday_start_ = *start;
  this->holiday_length_ = *length;
  ESP_LOGD(TAG, "Holiday: starts in %d days, length %d days", *start, *length);
  if (this->holiday_start_number_)
    this->holiday_start_number_->publish_state(*start);
  if (this->holiday_length_number_)
    this->holiday_length_number_->publish_state(*length);
}

#ifdef USE_TIME
void SmartBoiler::on_set_time(int64_t time_adjustment) {
  ESP_LOGI(TAG, "Adjusting time by: %" PRId64 " seconds", time_adjustment);
  auto cmd = SBProtocolRequest(SBC_PACKET_HOME_SETTIME, this->next_packet_uid_());
  cmd.write_le(uint64_t(time_adjustment));
  this->enqueue_command_(cmd);
}
#endif

void SmartBoiler::gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                                      esp_ble_gattc_cb_param_t *param) {
  switch (event) {
    case ESP_GATTC_OPEN_EVT: {
      if (param->open.status == ESP_GATT_OK) {
        ESP_LOGI(TAG, "[%s] Connected", this->parent_->address_str());
      }
      this->publish_monitoring_();
      break;
    }
    case ESP_GATTC_DISCONNECT_EVT: {
      ESP_LOGI(TAG, "[%s] Disconnected", this->parent_->address_str());
      this->gatt_ready_ = false;
      this->clear_queues_();
      this->set_state(ConnectionState::DISCONNECTED);
      this->publish_monitoring_();
      break;
    }
    case ESP_GATTC_SEARCH_CMPL_EVT: {
      auto chr = this->parent_->get_characteristic(SB_MAIN_SERVICE_UUID, SB_MAIN_CHARACTERISTIC_UUID);
      if (chr == nullptr) {
        ESP_LOGE(TAG, "[%s] No main service found at device, not a SmartBoiler..?",
                 this->parent_->address_str());
        this->parent_->disconnect();
        break;
      }

      this->char_handle_ = chr->handle;

      chr = this->parent_->get_characteristic(SB_LOGGING_SERVICE_UUID, SB_LOGGING_CHARACTERISTIC_UUID);

      if (chr == nullptr) {
        ESP_LOGE(TAG, "[%s] No logging service found at device, not a SmartBoiler..?",
                 this->parent_->address_str());
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
      this->gatt_ready_ = true;
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
  this->auth_sent_at_ = millis();
  this->enqueue_command_(cmd);
  if (this->state_ != ConnectionState::AUTHENTICATING)
    this->set_state(ConnectionState::AUTHENTICATING);
}

void SmartBoiler::getInitData() {
  ESP_LOGD(TAG, "Requesting initial data from water heater");
  this->request_value(SBPacket::SBC_PACKET_HOME_FWVERSION);
  this->request_value(SBPacket::SBC_PACKET_HOME_MODE);
  this->request_value(SBPacket::SBC_PACKET_HOME_BOILERMODEL);
  this->request_value(SBPacket::SBC_PACKET_HOME_CAPACITY);
  this->request_value(SBPacket::SBC_PACKET_HOME_BOILERNAME);
  this->request_value(SBPacket::SBC_PACKET_HOME_TEMPERATURE);
  this->request_value(SBPacket::SBC_PACKET_HOME_SENSOR1);
  this->request_value(SBPacket::SBC_PACKET_HOME_SENSOR2);
  this->request_value(SBPacket::SBC_PACKET_HOME_HSRCSTATE);
  this->request_value(SBPacket::SBC_PACKET_HDO_ONOFF);
  this->request_value(SBPacket::SBC_PACKET_HDO_LESSEXPTARIFFAVAILABLENOW);
  this->request_value(SBPacket::SBC_PACKET_HOME_ERROR);
  this->request_value(SBPacket::SBC_PACKET_HOME_TEMPNIGHTLOW);
  this->request_value(SBPacket::SBC_PACKET_HOME_TEMPNIGHT);
  this->request_value(SBPacket::SBC_PACKET_HOME_TEMPNIGHTCURR);
  this->request_value(SBPacket::SBC_PACKET_HOLIDAY_GET);
  this->request_value(SBPacket::SBC_PACKET_ANODE_VOLTAGE);
  // anti-legionella state is requested once FW version is known (requires FW >= 1.6)
  this->enqueue_command_(SBProtocolRequest(SBC_PACKET_STATISTICS_GETALL, this->next_packet_uid_()));
}

void SmartBoiler::update() {
  if (this->state_ == ConnectionState::CONNECTED) {
    ESP_LOGD(TAG, "Requesting consumption");
    auto cmd = SBProtocolRequest(SBC_PACKET_STATISTICS_GETALL, this->next_packet_uid_());
    this->enqueue_command_(cmd);
    this->request_value(SBPacket::SBC_PACKET_ANODE_VOLTAGE);
    this->request_value(SBPacket::SBC_PACKET_HOME_ERROR);
    this->request_value(SBPacket::SBC_PACKET_HOME_TEMPNIGHTCURR);
    this->request_value(SBPacket::SBC_PACKET_HOLIDAY_GET);
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
                                          [&](const SBProtocolRequest& req) { return req.mUid == (result.mUid & 0xFF); });
      if (originalRequest != this->sent_queue_.end()) {
        auto originalType = originalRequest->mRqType;
        ESP_LOGD(TAG, "original request was: %d", originalType);
        // remove the sent request from the queue as it was sucessfully accepted
        this->sent_queue_.erase(originalRequest);

        // handle some special confirm packets
        switch (originalType) {
          case SBPacket::SBC_PACKET_STATISTICS_GETALL: {
            if (result.mByteData.size() < 4) {
              ESP_LOGW(TAG, "Consumption response too short");
              break;
            }
            uint32_t consumption = result.load_uint32_le(0);
            if (this->consumption_sensor_)
              this->consumption_sensor_->publish_state((float) consumption / 1000);
            break;
          }
          default:
            this->refresh_after_(originalType);
            break;
        }
      }
      break;
    }
    case SBPacket::SBC_PACKET_GLOBAL_ERRORUID: {
      // water heater rejected request with given UID
      auto uidOpt = parse_number<int>(result.mString);
      if (!uidOpt.has_value()) {
        ESP_LOGW(TAG, "Bad error UID from water heater: %s", result.mString.c_str());
        break;
      }
      auto rejected = std::find_if(this->sent_queue_.begin(), this->sent_queue_.end(),
                                   [&](const SBProtocolRequest &req) { return req.mUid == (*uidOpt & 0xFF); });
      if (rejected != this->sent_queue_.end()) {
        auto rejectedType = rejected->mRqType;
        ESP_LOGW(TAG, "Water heater rejected request %d (UID %d)", rejectedType, rejected->mUid);
        this->sent_queue_.erase(rejected);
        // revert HA entities to the real state
        this->refresh_after_(rejectedType);
      } else {
        ESP_LOGW(TAG, "Water heater rejected unknown request UID %d", *uidOpt);
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
          auto fwOpt = parse_number<float>(fwVersion);
          bool firstTime = this->fw_version_ == 0;
          if (fwOpt.has_value())
            this->fw_version_ = *fwOpt;
          if (firstTime && this->fw_version_ >= MIN_FW_ANTILEGIONELLA && this->antilegionella_switch_)
            this->request_value(SBC_PACKET_HOME_ANTILEGIO);
          char buffer[100];
          sprintf(buffer, "fw:%s, board: %s, S/N: %s", fwVersion.c_str(), boardRev.c_str(), serial.c_str());
          std::string version(buffer);
          if (this->version_)
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
      this->current_mode_ = modeOpt.value();
      this->publish_mode_(this->current_mode_);
      break;
    }
    case SBPacket::SBC_PACKET_HOME_TEMPERATURE: {
      auto tempOpt = parse_number<float>(result.mString);
      if (tempOpt.has_value() && this->thermostat_) {
        this->thermostat_->publish_target_temp(*tempOpt);
      }
      break;
    }
    case SBPacket::SBC_PACKET_HOME_SENSOR1: {
      auto tempOpt = parse_number<float>(result.mString);
      if (tempOpt.has_value() && this->temperature_sensor_1_sensor_) {
        this->temperature_sensor_1_sensor_->publish_state(*tempOpt);
      }
      break;
    }
    case SBPacket::SBC_PACKET_HOME_SENSOR2: {
      auto tempOpt = parse_number<float>(result.mString);
      if (tempOpt.has_value()) {
        if (this->temperature_sensor_2_sensor_)
          this->temperature_sensor_2_sensor_->publish_state(*tempOpt);
        if (this->thermostat_)
          this->thermostat_->publish_current_temp(*tempOpt);
      }
      break;
    }
    case SBPacket::SBC_PACKET_HOME_HSRCSTATE: {
      auto heatOpt = parse_number<int>(result.mString);
      if (!heatOpt.has_value()) {
        ESP_LOGW(TAG, "Bad heat source state from water heater: %s", result.mString.c_str());
        break;
      }
      auto is_heating = *heatOpt == 1;
      if (this->heat_on_sensor_)
        this->heat_on_sensor_->publish_state(is_heating);
      if (thermostat_)
        this->thermostat_->publish_action(is_heating);
      break;
    }
    case SBPacket::SBC_PACKET_HDO_ONOFF: {
      auto hdoOpt = parse_number<int>(result.mString);
      if (!hdoOpt.has_value()) {
        ESP_LOGW(TAG, "Bad HDO state from water heater: %s", result.mString.c_str());
        break;
      }
      this->isHdoEnabled = *hdoOpt == 1;
      if (this->hdo_low_tariff_sensor_)
        this->hdo_low_tariff_sensor_->publish_state(this->isHdoEnabled);
      if (this->hdo_switch_)
        this->hdo_switch_->publish_state(this->isHdoEnabled);
      ESP_LOGI(TAG, "Internal HDO decoder is %s.", this->isHdoEnabled ? "enabled" : "disabled");
      break;
    }
    case SBPacket::SBC_PACKET_HOME_BOILERNAME: {
      if (this->name_)
        this->name_->publish_state(result.mString);
      break;
    }
    case SBPacket::SBC_PACKET_HOME_BOILERMODEL: {
      ESP_LOGD(TAG, "Water heater model: %s", result.mString.c_str());
      if (this->model_txt_)
        this->model_txt_->publish_state(result.mString);
      break;
    }
    case SBPacket::SBC_PACKET_HOME_CAPACITY: {
      ESP_LOGD(TAG, "Water heater capacity: %s l", result.mString.c_str());
      auto capOpt = parse_number<float>(result.mString);
      if (capOpt.has_value() && this->capacity_sensor_)
        this->capacity_sensor_->publish_state(*capOpt);
      break;
    }
    case SBPacket::SBC_PACKET_HOME_ERROR: {
      // bitmask of errors (Er01..Er09 in the original app)
      auto errOpt = parse_number<uint32_t>(result.mString);
      if (!errOpt.has_value()) {
        ESP_LOGW(TAG, "Bad error state from water heater: %s", result.mString.c_str());
        break;
      }
      this->publish_error_(*errOpt);
      break;
    }
    case SBPacket::SBC_PACKET_HDO_LESSEXPTARIFFAVAILABLENOW: {
      auto ltOpt = parse_number<int>(result.mString);
      if (!ltOpt.has_value()) {
        ESP_LOGW(TAG, "Bad low tariff state from water heater: %s", result.mString.c_str());
        break;
      }
      if (this->low_tariff_sensor_)
        this->low_tariff_sensor_->publish_state(*ltOpt > 0);
      break;
    }
    case SBPacket::SBC_PACKET_HOME_ANTILEGIO: {
      auto alOpt = parse_number<int>(result.mString);
      if (alOpt.has_value() && this->antilegionella_switch_)
        this->antilegionella_switch_->publish_state(*alOpt > 0);
      break;
    }
    case SBPacket::SBC_PACKET_HOME_TEMPNIGHTLOW:
    case SBPacket::SBC_PACKET_HOME_TEMPNIGHT: {
      auto tOpt = parse_number<float>(result.mString);
      if (!tOpt.has_value()) {
        ESP_LOGW(TAG, "Bad PROG temperature from water heater: %s", result.mString.c_str());
        break;
      }
      int t = (int) lroundf(*tOpt);
      if (result.mRqType == SBPacket::SBC_PACKET_HOME_TEMPNIGHTLOW) {
        this->prog_temp_low_ = t;
        if (this->prog_temp_low_number_)
          this->prog_temp_low_number_->publish_state(t);
      } else {
        this->prog_temp_high_ = t;
        if (this->prog_temp_high_number_)
          this->prog_temp_high_number_->publish_state(t);
      }
      break;
    }
    case SBPacket::SBC_PACKET_HOME_TEMPNIGHTCURR: {
      auto tOpt = parse_number<float>(result.mString);
      if (tOpt.has_value() && this->prog_temperature_sensor_)
        this->prog_temperature_sensor_->publish_state(*tOpt);
      break;
    }
    case SBPacket::SBC_PACKET_HOLIDAY_GET: {
      this->parse_holiday_(result.mString);
      break;
    }
    
    case SBPacket::SBC_PACKET_ANODE_VOLTAGE: {
      ESP_LOGD(TAG, "Anode voltage: %s", result.mString.c_str());
      auto voltageValue = parse_number<float>(result.mString);
      if (!voltageValue.has_value()) {
        ESP_LOGW(TAG, "Bad anode voltage from water heater: %s", result.mString.c_str());
        break;
      }
      auto voltage = *voltageValue / 1000.0;
      if (this->anode_voltage_sensor_)
        this->anode_voltage_sensor_->publish_state(voltage);
      break;
    }

    // Time is sent by water heater every 5 seconds.
    case SBPacket::SBC_PACKET_HOME_TIME: {
      // time is in format D.HH:MM:SS
      if (result.mString.size() < 10) {
        ESP_LOGW(TAG, "Bad time format from water heater: %s", result.mString.c_str());
        break;
      }
      // first byte is day
      auto day = result.mString[0] - '0';
      // the rest is a string
      auto time = result.mString.substr(2,8);
      
      char buffer[30];
      sprintf(buffer, "%s, %s", this->day_to_string(day), time.c_str());
      ESP_LOGD(TAG, "Water heater internal time: %s", buffer);
      
#ifdef USE_TIME
      // Parse the time string from water heater using strptime
      ESPTime water_heater_time;
      if (ESPTime::strptime(time.c_str(), water_heater_time)) {
        if (this->time_clock_ == nullptr) {
          ESP_LOGW(TAG, "No time_id configured on smartboiler; time sync skipped");
        } else {
        auto current_time = this->time_clock_->now();

        if (current_time.is_valid()) {
          // Convert water heater time to seconds since last Monday
          // Water heater: 0=Monday, 1=Tuesday, ..., 6=Sunday
          int water_heater_seconds_since_monday = (day * 24 * 3600) + 
                                                 (water_heater_time.hour * 3600) + 
                                                 (water_heater_time.minute * 60) + 
                                                 water_heater_time.second;
          
          // Convert current time to seconds since last Monday
          // ESPHome: 1=Sunday, 2=Monday, ..., 7=Saturday
          int current_day_of_week = current_time.day_of_week; // 1=Sunday, 2=Monday, etc.
          int days_since_monday = (current_day_of_week == 1) ? 6 : (current_day_of_week - 2);
          int current_seconds_since_monday = (days_since_monday * 24 * 3600) + 
                                            (current_time.hour * 3600) + 
                                            (current_time.minute * 60) + 
                                            current_time.second;
          
          // Calculate time difference in seconds
          int64_t time_difference = water_heater_seconds_since_monday - current_seconds_since_monday;

          ESP_LOGD(TAG, "Current ESPHome time: %04d-%02d-%02d %02d:%02d:%02d (day: %d, seconds since Monday: %d)", 
                   current_time.year, current_time.month, current_time.day_of_month,
                   current_time.hour, current_time.minute, current_time.second,
                   current_day_of_week, current_seconds_since_monday);
          
          char buffer[50];
          sprintf(buffer, "%s, %s (diff: %" PRId64 " sec)", this->day_to_string(day), time.c_str(), time_difference);
          ESP_LOGD(TAG, "Water heater internal time: %s", buffer);
          
          // If the time difference is too large, adjust the time
          // time is reported every 5 seconds - don't stack another adjustment while one is in flight
          if ((time_difference > TIME_ADJUSTMENT_THRESHOLD || time_difference < -TIME_ADJUSTMENT_THRESHOLD) &&
              !this->is_time_adjustment_pending_()) {
            ESP_LOGI(TAG, "Time difference is too large, adjusting time by %" PRId64 " seconds", time_difference);
            this->on_set_time(time_difference);
          }
        } else {
          ESP_LOGW(TAG, "Current time not available from ESPHome time component");
        }
        }
      } else {
        ESP_LOGW(TAG, "Failed to parse water heater time: %s", time.c_str());
      }
#else
      ESP_LOGD(TAG, "Time component not available - time synchronization disabled");
#endif
      break;
    }
    default:
      break;
  }
}

void SmartBoilerModeSelect::control(const std::string &value) { get_parent()->on_set_mode(value); }

void SmartBoilerSwitch::write_state(bool state) {
  if (this->type_ == SwitchType::MONITORING) {
    // enables/disables the BLE connection to the water heater (same as ble_client switch)
    ESP_LOGI(TAG, "Monitoring %s", state ? "enabled" : "disabled");
    this->get_parent()->parent()->set_enabled(state);
    this->publish_state(state);
    return;
  }
  if (this->get_parent()->state_ != ConnectionState::CONNECTED) {
    ESP_LOGW(TAG, "Not connected to water heater, switch change ignored.");
    return;
  }
  // state is published once the water heater confirms the change
  this->get_parent()->on_switch(this->type_, state);
}

void SmartBoilerNumber::control(float value) {
  if (this->get_parent()->state_ != ConnectionState::CONNECTED) {
    ESP_LOGW(TAG, "Not connected to water heater, value change ignored.");
    return;
  }
  // state is published once the water heater confirms the change
  this->get_parent()->on_number(this->type_, value);
}

void SmartBoilerResetEnergyButton::press_action() {
  if (this->get_parent()->state_ != ConnectionState::CONNECTED) {
    ESP_LOGW(TAG, "Not connected to water heater, energy reset ignored.");
    return;
  }
  this->get_parent()->on_reset_energy();
}

void SmartBoilerThermostat::control(const esphome::climate::ClimateCall &call) {
  if (call.get_mode().has_value() && *call.get_mode() != this->mode) {
    // operating mode is controlled via the mode select; republish to revert HA state
    ESP_LOGW(TAG, "Changing thermostat mode is not supported, use the mode select instead.");
    this->publish_state();
  }
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
  rv.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE);
  rv.add_feature_flags(climate::CLIMATE_SUPPORTS_ACTION);
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

void SmartBoilerThermostat::publish_mode(bool is_stopped) {
  this->mode = is_stopped ? esphome::climate::CLIMATE_MODE_OFF : esphome::climate::CLIMATE_MODE_HEAT;
  if (is_stopped)
    this->action = esphome::climate::CLIMATE_ACTION_OFF;
  this->publish_state();
}

void SmartBoilerThermostat::publish_action(bool heating) {
  if (this->mode == esphome::climate::CLIMATE_MODE_OFF)
    this->action = esphome::climate::CLIMATE_ACTION_OFF;
  else if (heating)
    this->action = esphome::climate::CLIMATE_ACTION_HEATING;
  else
    this->action = esphome::climate::CLIMATE_ACTION_IDLE;
  this->publish_state();
}

void SmartBoiler::publish_mode_(int mode) {
  auto modeAsString = this->convert_mode_to_action(mode);
  if (modeAsString.empty()) {
    ESP_LOGW(TAG, "Unknown mode value from water heater: %d", mode);
    return;
  }
  if (this->mode_select_)
    this->mode_select_->publish_state(modeAsString);
  if (this->thermostat_)
    this->thermostat_->publish_mode(mode == Mode::STOP);
}

void SmartBoiler::enqueue_command_(const SBProtocolRequest &command) {
  this->command_queue_.push_back(command);
  this->process_command_queue_();
}

void SmartBoiler::process_command_queue_() {
  uint32_t now = millis();
  uint32_t cmdDelay = now - this->last_command_timestamp_;

  if (this->gatt_ready_ && cmdDelay > COMMAND_DELAY && !this->command_queue_.empty()) {
    auto nextCmd = this->command_queue_.front();
    this->send_to_boiler(nextCmd);
    nextCmd.mLastSent = millis();
    if (nextCmd.mFirstSent == 0)
      nextCmd.mFirstSent = nextCmd.mLastSent;
    if (nextCmd.mUid) {
      this->sent_queue_.push_back(nextCmd);
    }
    this->command_queue_.erase(this->command_queue_.begin());
    ESP_LOGD(TAG, "Queue status - QUEUE=[%d], SENT_QUEUE=[%d]", this->command_queue_.size(), this->sent_queue_.size());
  }
}

std::string SmartBoiler::generateUUID() {
  const char hex_chars[] = "0123456789ABCDEF";
  std::string uid;
  uid.reserve(6);
  for (int i = 0; i < 6; i++) {
    uid += hex_chars[random_uint32() & 0xF];
  }
  return uid;
}



void SmartBoiler::send_pin(uint32_t pin) {
  ESP_LOGD(TAG, "Sending PIN to water heater.");
  auto cmd = SBProtocolRequest(SBC_PACKET_GLOBAL_PAIRPIN, this->next_packet_uid_());
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
  if (this->state_txt_)
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
optional<uint8_t> SmartBoiler::convert_action_to_mode(const std::string &payload) {
  auto modeStr = str_upper_case(payload);

  if (modeStr == MODE_ANTIFREEZE)
    return Mode::ANTIFREEZE;
  if (modeStr == MODE_SMART)
    return this->isHdoEnabled ? Mode::SMART_HDO : Mode::SMART;
  if (modeStr == MODE_PROG)
    return Mode::PROG;
  if (modeStr == MODE_MANUAL)
    return this->isHdoEnabled ? Mode::HDO : Mode::MANUAL;
  if (modeStr == MODE_STOP || modeStr == MODE_TEST || modeStr == MODE_HOLIDAY) {
    ESP_LOGW(TAG, "Mode %s cannot be set from ESPHome.", payload.c_str());
    return {};
  }
  ESP_LOGW(TAG, "Unknown water heater mode: %s", payload.c_str());
  return {};
}

std::string SmartBoiler::convert_mode_to_action(const uint8_t mode) {
  switch (mode) {
    case Mode::STOP:
      return MODE_STOP;
    case Mode::ANTIFREEZE:
      return MODE_ANTIFREEZE;
    case Mode::SMART:
    case Mode::SMART_HDO:
      return MODE_SMART;
    case Mode::PROG:
      return MODE_PROG;
    case Mode::MANUAL:
    case Mode::HDO:
      return MODE_MANUAL;
    case Mode::TEST:
      return MODE_TEST;
    case Mode::HOLIDAY:
      return MODE_HOLIDAY;
    default:
      return "";
  }
}

}  // namespace sb
}  // namespace esphome
