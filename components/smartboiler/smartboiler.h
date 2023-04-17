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
#include "SBProtocol.h"

namespace esphome {
namespace sb {

static const uint8_t MIN_TEMP = 40;
static const uint8_t MAX_TEMP = 80;

class SmartBoilerModeSelect;
class SmartBoilerThermostat;
class SmartBoilerPinInput;

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

 protected:
  void set_uid(const std::string &uid) { this->uid_ = uid; }
  void on_set_temperature(uint8_t temp);
  void on_set_mode(const std::string &payload);
  void on_set_hdo_enabled(const std::string &payload);
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
  uint8_t convert_action_to_mode(const std::string &payload);

  ESPPreferenceObject pref_;
  // state of the connection
  ConnectionState state_ = ConnectionState::DISCONNECTED;
  // random UID of this device. Needs to be registered in boiler via PIN pairing.
  std::string uid_;
  // incremental counter for packets which requires unique ID
  uint32_t mPacketUid = 1;
  uint32_t last_command_timestamp_;

  // Handle for outgoing requests
  uint16_t char_handle_;
  // queue of commands waiting to be send
  std::vector<SBProtocolRequest> command_queue_;
  // queue for sent command to later pair with responses
  std::vector<SBProtocolRequest> sent_queue_;

  sensor::Sensor *temperature_sensor_1_sensor_ = nullptr;
  sensor::Sensor *temperature_sensor_2_sensor_ = nullptr;
  sensor::Sensor *consumption_sensor_ = nullptr;

  binary_sensor::BinarySensor *hdo_low_tariff_sensor_ = nullptr;
  binary_sensor::BinarySensor *heat_on_sensor_ = nullptr;
  text_sensor::TextSensor *state_txt_ = nullptr;
  text_sensor::TextSensor *version_ = nullptr;
  text_sensor::TextSensor *name_ = nullptr;
  SmartBoilerModeSelect *mode_select_ = nullptr;
  SmartBoilerThermostat *thermostat_ = nullptr;
  SmartBoilerPinInput *mPin_ = nullptr;

  friend class SmartBoilerModeSelect;
  friend class SmartBoilerThermostat;
  friend class SmartBoilerPinInput;
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

  friend class SmartBoiler;
};

class SmartBoilerPinInput : public esphome::number::Number, public esphome::Parented<SmartBoiler> {
 protected:
  virtual void control(float value) override;
};

}  // namespace sb
}  // namespace esphome

#endif
