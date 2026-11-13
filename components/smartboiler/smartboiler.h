#pragma once

#ifndef SMARTBOILER_H
#define SMARTBOILER_H

#include "esphome/core/component.h"
#include "esphome/components/ble_client/ble_client.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/select/select.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/button/button.h"
#include "SBProtocol.h"

#define TIME_ADJUSTMENT_THRESHOLD 30

namespace esphome {
namespace time {
class RealTimeClock;
}
namespace sb {

static const uint8_t MIN_TEMP = 40;
static const uint8_t MAX_TEMP = 80;

static const std::string MODE_ANTIFREEZE = "ANTIFREEZE";
static const std::string MODE_SMART = "SMART";
static const std::string MODE_PROG = "PROG";
static const std::string MODE_MANUAL = "MANUAL";
// modes which are reported by the water heater, but cannot be selected
static const std::string MODE_STOP = "STOP";
static const std::string MODE_TEST = "TEST";
static const std::string MODE_HOLIDAY = "HOLIDAY";

// re-send authentication request if water heater did not respond (same as original app)
static const uint32_t AUTH_RETRY_INTERVAL = 4000;
// re-send unconfirmed packet after this time (same as original app)
static const uint32_t PACKET_RESEND_INTERVAL = 10000;
// give up waiting for confirmation after this time (same as original app)
static const uint32_t PACKET_EXPIRATION = 30000;
// how often the queue of sent packets is checked
static const uint32_t SENT_QUEUE_CHECK_INTERVAL = 1000;
// how often the "low tariff available now" state is polled
static const uint32_t LOW_TARIFF_POLL_INTERVAL = 60000;

// temperature limits for PROG (night) mode, same as original app
static const uint8_t PROG_MIN_TEMP = 10;
static const uint8_t PROG_MAX_TEMP = 80;
// max. number of days for holiday start / length
static const uint16_t HOLIDAY_MAX_DAYS = 365;

// minimal firmware versions for some features (from original app)
static const float MIN_FW_ANTILEGIONELLA = 1.6f;

class SmartBoilerModeSelect;
class SmartBoilerThermostat;
class SmartBoilerPinInput;
class SmartBoilerSwitch;
class SmartBoilerNumber;
class SmartBoilerResetEnergyButton;

enum class SwitchType : uint8_t { HDO, ANTILEGIONELLA };
enum class NumberType : uint8_t { PROG_TEMP_LOW, PROG_TEMP_HIGH, HOLIDAY_START, HOLIDAY_LENGTH };

enum class ConnectionState { DISCONNECTED, AUTHENTICATING, CONNECTED, NEED_PIN };

struct SavedSmartBoilerSettings {
  char uid[6];
} PACKED;

class SmartBoiler : public PollingComponent,
                    public esphome::ble_client::BLEClientNode {
 public:
  void setup() override;
  void update() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void gattc_event_handler(esp_gattc_cb_event_t event, esp_gatt_if_t gattc_if,
                           esp_ble_gattc_cb_param_t *param) override;

  void set_pin_input(SmartBoilerPinInput *n) { mPin_ = n; }
  void set_temp1(sensor::Sensor *s) { temperature_sensor_1_sensor_ = s; }
  void set_temp2(sensor::Sensor *s) { temperature_sensor_2_sensor_ = s; }
  void set_hdo_low_tariff(binary_sensor::BinarySensor *s) { hdo_low_tariff_sensor_ = s; }
  void set_heat_on(binary_sensor::BinarySensor *s) { heat_on_sensor_ = s; }
  void set_mode(SmartBoilerModeSelect *s) { mode_select_ = s; }
  void set_thermostat(SmartBoilerThermostat *t) { thermostat_ = t; }
  void set_consumption(sensor::Sensor *s) { consumption_sensor_ = s; }
  void set_state(text_sensor::TextSensor *t) { state_txt_ = t; }
  void set_version(text_sensor::TextSensor *t) { version_ = t; }
  void set_name(text_sensor::TextSensor *t) { name_ = t; }
  void set_anode_voltage(sensor::Sensor *s) { anode_voltage_sensor_ = s; }
  void set_time_clock(time::RealTimeClock *rtc) { this->time_clock_ = rtc; }
  void set_low_tariff(binary_sensor::BinarySensor *s) { low_tariff_sensor_ = s; }
  void set_problem(binary_sensor::BinarySensor *s) { problem_sensor_ = s; }
  void set_error(text_sensor::TextSensor *t) { error_txt_ = t; }
  void set_model(text_sensor::TextSensor *t) { model_txt_ = t; }
  void set_capacity(sensor::Sensor *s) { capacity_sensor_ = s; }
  void set_prog_temperature(sensor::Sensor *s) { prog_temperature_sensor_ = s; }
  void set_hdo_switch(SmartBoilerSwitch *s) { hdo_switch_ = s; }
  void set_antilegionella_switch(SmartBoilerSwitch *s) { antilegionella_switch_ = s; }
  void set_prog_temp_low_number(SmartBoilerNumber *n) { prog_temp_low_number_ = n; }
  void set_prog_temp_high_number(SmartBoilerNumber *n) { prog_temp_high_number_ = n; }
  void set_holiday_start_number(SmartBoilerNumber *n) { holiday_start_number_ = n; }
  void set_holiday_length_number(SmartBoilerNumber *n) { holiday_length_number_ = n; }

 protected:
  void set_uid(const std::string &uid) { this->uid_ = uid; }
  void on_set_temperature(uint8_t temp);
  void on_set_mode(const std::string &payload);
  void on_switch(SwitchType type, bool state);
  void on_number(NumberType type, float value);
  void on_reset_energy();
  void send_prog_temperatures_(int low, int high);
  void send_holiday_(int start, int length);
  void refresh_after_(SBPacket request);
  void publish_error_(uint32_t mask);
  void parse_holiday_(const std::string &payload);
  void handle_incoming(const uint8_t *data, uint16_t length);
  void request_value(SBPacket value, uint16_t uid = 0);
  void send_to_boiler(SBProtocolRequest request);
  void enqueue_command_(const SBProtocolRequest &command);
  void process_command_queue_();
  void send_pin(uint32_t pin);
  void authenticate();
  void getInitData();
  void restore_state_();
  void save_state_();
  void set_state(ConnectionState newState);
  std::string generateUUID();
  const char *state_to_string(ConnectionState state);
  const char *day_to_string(uint8_t day);
  optional<uint8_t> convert_action_to_mode(const std::string &payload);
  std::string convert_mode_to_action(const uint8_t mode);
  void on_set_time(int64_t time_adjustment);
  uint16_t next_packet_uid_();
  void check_sent_queue_();
  void clear_queues_();
  bool is_time_adjustment_pending_();
  void publish_mode_(int mode);

  ESPPreferenceObject pref_;
  // state of the connection
  ConnectionState state_ = ConnectionState::DISCONNECTED;
  // random UID of this device. Needs to be registered in boiler via PIN pairing.
  std::string uid_;
  // incremental counter for packets which requires unique ID
  uint16_t mPacketUid = 0;
  uint32_t last_command_timestamp_ = 0;
  // when the authentication request was last sent
  uint32_t auth_sent_at_ = 0;
  // when the queue of sent packets was last checked
  uint32_t last_sent_queue_check_ = 0;
  // true when the BLE characteristics are discovered and commands can be written
  bool gatt_ready_ = false;
  // last mode reported by water heater, -1 when unknown
  int current_mode_ = -1;
  // last values reported by water heater, -1 when unknown
  int prog_temp_low_ = -1;
  int prog_temp_high_ = -1;
  int holiday_start_ = -1;
  int holiday_length_ = -1;
  // firmware version of the water heater, 0 when unknown
  float fw_version_ = 0;
  uint32_t last_low_tariff_poll_ = 0;

  // Handle for outgoing requests
  uint16_t char_handle_;
  // queue of commands waiting to be send
  std::vector<SBProtocolRequest> command_queue_;
  // queue for sent command to later pair with responses
  std::vector<SBProtocolRequest> sent_queue_;

  sensor::Sensor *temperature_sensor_1_sensor_ = nullptr;
  sensor::Sensor *temperature_sensor_2_sensor_ = nullptr;
  sensor::Sensor *consumption_sensor_ = nullptr;
  sensor::Sensor *anode_voltage_sensor_ = nullptr;

  binary_sensor::BinarySensor *hdo_low_tariff_sensor_ = nullptr;
  binary_sensor::BinarySensor *heat_on_sensor_ = nullptr;
  text_sensor::TextSensor *state_txt_ = nullptr;
  text_sensor::TextSensor *version_ = nullptr;
  text_sensor::TextSensor *name_ = nullptr;
  SmartBoilerModeSelect *mode_select_ = nullptr;
  SmartBoilerThermostat *thermostat_ = nullptr;
  SmartBoilerPinInput *mPin_ = nullptr;
  time::RealTimeClock *time_clock_{nullptr};
  binary_sensor::BinarySensor *low_tariff_sensor_ = nullptr;
  binary_sensor::BinarySensor *problem_sensor_ = nullptr;
  text_sensor::TextSensor *error_txt_ = nullptr;
  text_sensor::TextSensor *model_txt_ = nullptr;
  sensor::Sensor *capacity_sensor_ = nullptr;
  sensor::Sensor *prog_temperature_sensor_ = nullptr;
  SmartBoilerSwitch *hdo_switch_ = nullptr;
  SmartBoilerSwitch *antilegionella_switch_ = nullptr;
  SmartBoilerNumber *prog_temp_low_number_ = nullptr;
  SmartBoilerNumber *prog_temp_high_number_ = nullptr;
  SmartBoilerNumber *holiday_start_number_ = nullptr;
  SmartBoilerNumber *holiday_length_number_ = nullptr;

  friend class SmartBoilerModeSelect;
  friend class SmartBoilerThermostat;
  friend class SmartBoilerPinInput;
  friend class SmartBoilerSwitch;
  friend class SmartBoilerNumber;
  friend class SmartBoilerResetEnergyButton;

private:
  bool isHdoEnabled = false;
  
};

class SmartBoilerModeSelect : public esphome::select::Select, public esphome::Parented<SmartBoiler> {
 protected:
  virtual void control(const std::string &value) override;
};

class SmartBoilerThermostat : public esphome::climate::Climate, public esphome::Parented<SmartBoiler> {
 protected:
  virtual void control(const esphome::climate::ClimateCall &call) override;
  virtual esphome::climate::ClimateTraits traits() override;

  void publish_target_temp(float temp);
  void publish_current_temp(float temp);
  void publish_action(bool is_heating);
  void publish_mode(bool is_stopped);

  friend class SmartBoiler;
};

class SmartBoilerPinInput : public esphome::number::Number, public esphome::Parented<SmartBoiler> {
 protected:
  virtual void control(float value) override;
};

class SmartBoilerSwitch : public esphome::switch_::Switch, public esphome::Parented<SmartBoiler> {
 public:
  void set_type(SwitchType type) { this->type_ = type; }

 protected:
  void write_state(bool state) override;
  SwitchType type_{SwitchType::HDO};
};

class SmartBoilerNumber : public esphome::number::Number, public esphome::Parented<SmartBoiler> {
 public:
  void set_type(NumberType type) { this->type_ = type; }

 protected:
  void control(float value) override;
  NumberType type_{NumberType::PROG_TEMP_LOW};
};

class SmartBoilerResetEnergyButton : public esphome::button::Button, public esphome::Parented<SmartBoiler> {
 protected:
  void press_action() override;
};

}  // namespace sb
}  // namespace esphome

#endif
