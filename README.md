# ESPHome component for Dražice OKHE smart water heater

This ESPHome component connects to Dražice OKHE smart water heaters via Bluetooth Low Energy, collects data, and also allows for limited remote control.

You will need an ESP32 module and an MQTT server. You also need to determine the MAC address of your water heater. See an [example configuration](example.yaml).

Note that only one device can be connected to the water heater at a time.

## Hardware

This component has been tested on:
* ESP32 dev board
* ESP32-C3

## Documentation

This ESPHome component uses several standard components (sensors, etc.) for easy Home Assistant integration (auto-discovery). While it uses MQTT, it's also possible to remove MQTT and set up HA integration directly via the `api:` option.

### Water heater outputs

| Binary sensors | |
| --- | --- |
| HDO | On if HDO detection is enabled in the water heater |
| Heat | On when the water heater is currently heating (consuming energy) |

| Sensors | |
| --- | --- |
| Temp1  | Temperature detected by lower temperature sensor |
| Temp2  | Temperature detected by upper temperature sensor (this is the temperature displayed on the water heater) |
| Normal temperature | Configured target temperature in NORMAL/HDO mode |
| Energy | Energy consumed since last reset (in kWh) |

| Text sensors | |
| --- | --- |
| Version | Firmware version, board version, and serial number |
| State | Connection state: Disconnected / Authenticating / Require PIN / Connected |
| Name  | Name of the water heater unit |

| Inputs | |
| --- | --- |
| Pairing PIN | Input to set pairing PIN |
| Mode  | Set one of the operating modes |

### Modes

This is the list of recognized mode values. See the [original app](https://play.google.com/store/apps/details?id=cz.dzd.smartbojler&hl=cs&gl=US) for more details, but the names are self-explanatory.

| Value           | Notes                       |
| --------------- | --------------------------- |
| STOP            | Cannot be set               |
| NORMAL          | Can be set if hdo_enabled=0 |
| HDO             | Can be set if hdo_enabled=1 |
| SMART           | Can be set if hdo_enabled=0 |
| SMARTHDO        | Can be set if hdo_enabled=1 |
| ANTIFROST       |                             |
| NIGHT           |                             |

Toggling mode will also enable/disable HDO based on the selected mode - NORMAL/HDO and SMART/SMARTHDO.

## Pairing process

The water heater requires the client to be "authenticated" in order to communicate. The client generates a random UUID, sends it to the water heater, and the water heater responds with a request for pairing and shows a PIN on the display.

This component allows you to set the pairing PIN and complete the pairing process from Home Assistant.

### 1. Configure ESP device

Create a configuration for ESP (see [example](example.yaml) for details). The component allows you to define multiple water heaters to connect to (max 3). You need to know the water heater's Bluetooth MAC address (use any BT scanner to listen for BLE announcements. Please note that the water heater sends announcements only when no device is connected).

```yaml
esphome:
  name: "test-node"

esp32:
  board: nodemcu-32s
  framework:
    type: esp-idf
    version: recommended

substitutions:
  mac_water_heater: XX:XX:XX:XX:XX:XX

# Enable time component. This will be used to set the time on the water heater.
time:
  - platform: homeassistant
    id: esphome_time

external_components:
  - source: 
      type: git
      url: https://github.com/pedobry/esphome-smartboiler
    components: [smartboiler]

logger:
  level: info

# Enable Home Assistant API
api:
  encryption:
    key: !secret ha_API_key

ble_client:
  - mac_address: ${mac_water_heater}
    id: drazice_okhe

smartboiler:
  - ble_client_id: drazice_okhe
    time_id: esphome_time
    temp1:
      name: "Water heater temp1"
    temp2:
      name: "Water heater temp2"
    mode:
      name: "Water heater mode"
    thermostat:
      name: "Water heater thermostat"
    heat_on:
      name: "Water heater heat"
    hdo_low_tariff:
      name: "Water heater HDO"
    pin:
      name: "Water heater PIN"
    state:
      name: "Water heater state"
    version:
      name: "Water heater version"
    b_name:
      name: "Water heater name"
    consumption:
      name: "Water heater energy"

switch:
  - platform: ble_client
    ble_client_id: drazice_okhe
    name: "Enable water heater monitoring"

ota:
  password: !secret ota_password

wifi:
  ssid: !secret wifi1_ssid
  password: !secret wifi1_psk

```

I strongly recommend adding a switch to enable/disable the monitoring. The water heater allows only one device to connect at a time, and you might need to disconnect the monitoring when you want to use the mobile application for some complex settings or firmware upgrades.

### 2. Pairing with PIN

Once you flash the ESP32 and power it on, it will auto-generate a random UUID and try to connect to the water heater. Since it's not paired yet, it will end up in the `Require PIN` state (with the PIN visible on the water heater display). At this moment, you can enter the PIN in Home Assistant and it will finish the pairing process.

> **Important:**\
> The water heater keeps the PIN visible only for about 20 seconds, then it disconnects the client and the pairing process restarts. The water heater always generates a new PIN on each attempt. Wait for HA until it shows the state `Require PIN` and only then enter the PIN.\
\
Changing/setting the PIN in other states is ignored.\
Once the pairing is completed (state is `Connected`), the PIN value is also ignored.

The ESP32 stores the UUID in NVM and pairing is persistent between restarts.

### Setting time on water heater
The water heater has internal clock which measure date-less time (only day of the week and hours, minutes and seconds). This is used for scheduling of water heater stated or temperatures (available only in original app). For that, it's necessary that water heater has proper time set up. Unfortunately, the time is lost after power outage and needs to be set up again.

Add a `time:` source (for example `platform: homeassistant`) and set `time_id` on each `smartboiler` entry to the same `id` as that clock. The firmware then compares the heater clock to ESPHome and adjusts if it differs by more than 30 seconds, keeping the heater aligned with Home Assistant.

### How to reset UUID

ESPHome NVM can be reset by:

```bash
dd if=/dev/zero of=nvs_zero bs=1 count=20480
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x009000 nvs_zero
```

## Multiple water heaters on the same ESP32

It's possible to connect an ESP to multiple water heaters simultaneously (up to three). See [example_multiple.yaml](example_multiple.yaml).

**IMPORTANT:**

When testing multiple water heaters (BLE clients) on ESPHome 2023.3.x, I have found it to be very unstable (keeps restarting frequently, disconnects, even up to the point where it's not possible to finish OTA updates). When you want to define multiple heaters on a single ESP, I recommend using the older **ESPHome 2022.11.5** which I found somewhat stable.

As a general rule, given the price of ESP32 devices, it's best to use a dedicated ESP32 for each water heater.

![Home Assistant](HA.png)
