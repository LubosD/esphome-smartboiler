# ESPHome component for Dražice OKHE smart water heater

This ESPHome component connects to Dražice OKHE smart water heaters via Bluetooth Low Energy, collects data, and also allows for limited remote control.

> [!WARNING]
> **Upgrading to v2.0.0:** the YAML configuration format has changed and in some cases the water heater may need to be **paired again** (enter the PIN shown on the water heater in Home Assistant). See [Upgrading from older versions](#upgrading-from-older-versions) and the [changelog](CHANGELOG.md). To stay on the previous version, use `ref: v1.0.0` in `external_components`.

You will need an ESP32 module and Home Assistant with the ESPHome integration. You also need to determine the MAC address of your water heater. See an [example configuration](example.yaml).

Note that only one device can be connected to the water heater at a time.

## Contents

- [Hardware](#hardware)
- [Documentation](#documentation)
  - [Entities](#entities)
  - [Holiday](#holiday)
  - [Modes](#modes)
- [Versions](#versions)
- [Upgrading from older versions](#upgrading-from-older-versions)
- [Pairing process](#pairing-process)
  - [1. Configure ESP device](#1-configure-esp-device)
  - [2. Pairing with PIN](#2-pairing-with-pin)
- [Tips](#tips)
  - [Setting time on water heater](#setting-time-on-water-heater)
  - [How to reset UUID](#how-to-reset-uuid)

## Hardware

This component has been tested on:
* ESP32 dev board
* ESP32-C3

## Documentation

This ESPHome component uses standard ESPHome entities (sensors, switches, etc.), which are discovered automatically by Home Assistant through the native ESPHome API (`api:`).

![Water heater in Home Assistant](home-assistant.png)

### Entities

All entities are created automatically with the default names below. In Home Assistant they are shown under the ESPHome device, prefixed with its name (set `friendly_name` in the `esphome:` section, e.g. `Water heater` → `Water heater Upper temperature`).

| Default name | Config key | Type | Description |
| --- | --- | --- | --- |
| Monitoring | `monitoring` | switch | Enables/disables the connection to the water heater, see below |
| Thermostat | `thermostat` | climate | Target temperature for MANUAL/HDO mode (40–80 °C), current (upper) temperature and heating action |
| Mode | `mode` | select | Operating mode, see below |
| Lower temperature | `temp1` | sensor | Temperature detected by the lower temperature sensor |
| Upper temperature | `temp2` | sensor | Temperature detected by the upper temperature sensor (displayed on the water heater) |
| Heating | `heat_on` | binary sensor | On when the water heater is currently heating (consuming energy) |
| Energy | `consumption` | sensor | Energy consumed since the last reset (kWh) |
| Reset energy | `reset_energy` | button | Resets the energy counter in the water heater |
| Low tariff | `low_tariff` | binary sensor | On when the low (cheaper) tariff is available now, polled every 60 s |
| HDO decoder | `hdo` | switch | Enables/disables the internal HDO (ripple control) decoder |
| HDO | `hdo_low_tariff` | binary sensor | Same as the HDO decoder switch, kept for compatibility (disabled by default) |
| PROG temperature | `prog_temperature` | sensor | Target temperature currently used by the PROG (night) program |
| PROG low / high temperature | `prog_temp_low`, `prog_temp_high` | number | Temperatures used by the PROG program, 10–80 °C, low must be lower than high |
| Holiday starts in | `holiday_start` | number | Days until the holiday starts (0 = today) |
| Holiday length | `holiday_length` | number | Holiday length in days, `0` cancels the holiday |
| Anti-legionella | `antilegionella` | switch | Periodic anti-legionella heating (water heater FW 1.6 or newer) |
| Problem | `problem` | binary sensor | On when the water heater reports any error |
| Error | `error` | text sensor | `OK` or list of active errors (codes Er01–Er09 as in the original app) |
| Pairing PIN | `pin` | number | PIN for pairing, see below |
| State | `state` | text sensor | Connection state: Disconnected / Authenticating / Require PIN / Connected |
| Anode Voltage | `anode_voltage` | sensor | Voltage of the protective anode |
| Name | `b_name` | text sensor | Name of the water heater |
| Model | `model` | text sensor | Model of the water heater |
| Version | `version` | text sensor | Firmware version, board version and serial number |
| Capacity | `capacity` | sensor | Configured volume of the water heater (l) |

Any entity can be customized using its config key with standard ESPHome entity options:

```yaml
smartboiler:
  temp2:
    name: "Water temperature"   # rename
  model:
    internal: true              # don't expose to Home Assistant
  anode_voltage:
    disabled_by_default: true   # create in Home Assistant, but disabled
```

Switches and numbers show the state reported by the water heater. After a change, the value is re-read from the water heater, so a rejected change is reverted in Home Assistant.

### Holiday

During a holiday the water heater only protects the water from freezing (mode `HOLIDAY`, same as ANTIFREEZE) and doesn't heat water you won't use. The holiday is set by two numbers, both counted in days **relative to today**:

| Entity | Meaning |
| --- | --- |
| *Holiday starts in* | Days until the first day of the holiday: `0` = today, `1` = tomorrow, ... |
| *Holiday length* | Number of holiday days including the first and the last day. `0` = no holiday |

The holiday covers the days from *today + Holiday starts in* to *today + Holiday starts in + Holiday length − 1*.

**Planning a holiday**

1. Set *Holiday starts in*. While *Holiday length* is `0`, the value is only remembered by the ESP and nothing is sent to the water heater yet.
2. Set *Holiday length*. Now both values are sent to the water heater and read back, so Home Assistant shows what the water heater really accepted.

Example: today is Monday and you will be away from Saturday to the next Sunday (9 days). Set *Holiday starts in* to `5` and *Holiday length* to `9`.

**Changing or cancelling a holiday**

* Changing either number of a planned holiday sends the new values to the water heater immediately.
* Setting *Holiday length* to `0` cancels the holiday.

Notes:

* The water heater counts the days down by itself (based on its internal clock), so *Holiday starts in* decreases every day. Keep the clock in sync by configuring a `time:` source.
* The values are also shown and can be changed in the original mobile app (*Vacation*).
* The *Mode* select shows `HOLIDAY` while the holiday is active. It cannot be selected manually.

### Modes

This is the list of recognized mode values. See the [original app](https://play.google.com/store/apps/details?id=cz.dzd.smartbojler&hl=cs&gl=US) for more details, but the names are self-explanatory.

| Value      | Water heater mode           | Notes                                                    |
| ---------- | --------------------------- | -------------------------------------------------------- |
| MANUAL     | NORMAL (1) / HDO (2)        | HDO variant is used when the internal HDO decoder is on  |
| SMART      | SMART (3) / SMARTHDO (4)    | HDO variant is used when the internal HDO decoder is on  |
| ANTIFREEZE | ANTIFROST (5)               |                                                          |
| PROG       | NIGHT (6)                   | Program/schedule is configured in the original app       |
| STOP       | STOP (0)                    | Reported only, cannot be set                             |
| TEST       | TEST (7)                    | Reported only, cannot be set                             |
| HOLIDAY    | HOLIDAY (8)                 | Reported only, holiday is configured in the original app |

The thermostat entity shows `off` when the water heater is in STOP mode and `heat` otherwise. Its mode cannot be changed; use the mode select instead.


## Versions

Releases are tagged (`v1.0.0`, `v2.0.0`, ...), see the [changelog](CHANGELOG.md). Without `ref`, ESPHome downloads the latest version of the default branch (refreshed daily), which can include changes requiring an update of your YAML. To control when you upgrade, pin a version:

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/pedobry/esphome-smartboiler
      ref: v2.0.0
    components: [smartboiler]
```

## Upgrading from older versions

When ESPHome finds an old configuration, it shows a warning with a link to this section. If you are not ready to upgrade, pin `ref: v1.0.0`.

* `smartboiler:` is no longer a list - remove the leading `- ` and fix the indentation. A list with one item still works (with a warning), more water heaters on one ESP are not supported anymore.
* `ble_client_id` and `time_id` can be omitted when there is only one BLE client / time source.
* The monitoring switch is created by the component - remove the `switch:` with `platform: ble_client` (otherwise there would be two switches doing the same).
* Entity names are now optional, you can remove them right away. The new version tries to find and migrate the stored pairing UID on the first start, so the pairing is usually kept. If the *State* entity shows `Require PIN` after the upgrade (e.g. the ESP was used for more water heaters before), pair the water heater again - see [Pairing with PIN](#2-pairing-with-pin).

## Pairing process

The water heater requires the client to be "authenticated" in order to communicate. The client generates a random UUID, sends it to the water heater, and the water heater responds with a request for pairing and shows a PIN on the display.

This component allows you to set the pairing PIN and complete the pairing process from Home Assistant.

### 1. Configure ESP device

Create a configuration for ESP (see [example](example.yaml) for details). You need to know the water heater's Bluetooth MAC address (use any BT scanner to listen for BLE announcements. Please note that the water heater sends announcements only when no device is connected).

```yaml
esphome:
  name: water-heater
  friendly_name: Water heater

esp32:
  board: nodemcu-32s
  framework:
    type: esp-idf

external_components:
  - source:
      type: git
      url: https://github.com/pedobry/esphome-smartboiler
    components: [smartboiler]

logger:

# Enable Home Assistant API
api:
  encryption:
    key: !secret ha_API_key

ota:
  - platform: esphome
    password: !secret ota_password

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

# Optional: used to keep the water heater clock in sync (used automatically when present)
time:
  - platform: homeassistant

ble_client:
  - mac_address: XX:XX:XX:XX:XX:XX

smartboiler:

```

The *Monitoring* switch enables/disables the connection to the water heater. The water heater allows only one device to be connected at a time, so turn the monitoring off when you want to use the mobile application (e.g. for firmware upgrades). The connection is always enabled after the ESP restarts.

### 2. Pairing with PIN

Once you flash the ESP32 and power it on, it will auto-generate a random UUID and try to connect to the water heater. Since it's not paired yet, it will end up in the `Require PIN` state (with the PIN visible on the water heater display). At this moment, you can enter the PIN in Home Assistant and it will finish the pairing process.

> **Important:**\
> The water heater keeps the PIN visible only for about 20 seconds, then it disconnects the client and the pairing process restarts. The water heater always generates a new PIN on each attempt. Wait for HA until it shows the state `Require PIN` and only then enter the PIN.\
\
Changing/setting the PIN in other states is ignored.\
Once the pairing is completed (state is `Connected`), the PIN value is also ignored.

The ESP32 stores the UUID in NVM and pairing is persistent between restarts.

## Tips

### Setting time on water heater
The water heater has internal clock which measure date-less time (only day of the week and hours, minutes and seconds). This is used for scheduling of water heater stated or temperatures (available only in original app). For that, it's necessary that water heater has proper time set up. Unfortunately, the time is lost after power outage and needs to be set up again.

Add a `time:` source (for example `platform: homeassistant`). It is used automatically; if you have more than one time source, select it with `time_id`. The firmware then compares the heater clock to ESPHome and adjusts if it differs by more than 30 seconds, keeping the heater aligned with Home Assistant.

### How to reset UUID

ESPHome NVM can be reset by:

```bash
dd if=/dev/zero of=nvs_zero bs=1 count=20480
esptool.py --chip esp32 --port /dev/ttyUSB0 write_flash 0x009000 nvs_zero
```
