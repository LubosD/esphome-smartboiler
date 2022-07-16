# ESPHome component for Dražice OKHE smart boilers

This ESPHome components connects to Dražice OKHE smart boilers via Bluetooth Low Energy, collects data and also allows for limited remote control.

You will need an ESP32 module and an MQTT server. You also need to determine the MAC address of your boiler. See an [example configuration](example.yaml).

Take note that only one device can be connected to the boiler at a time.

## Documentation

### Boiler outputs

Description of some (but not all) of the topics set by this component:

| Topic          | Description |
| -------------- | ----------- |
| temperature    | Configured target temperature. |
| hdo_low_tariff | Whether low energy tariff is currently active (0/1). |
| hdo_enabled    | Whether HDO low energy tariff detection is enabled (0/1). |
| boiler_online  | Whether the boiler is currently connected (0/1). An MQTT last will message or BT disconnect will set this to 0. |
| sensor1        | Temperature detected by lower temp sensor. |
| sensor2        | Temperature detected by upper temp sensor (this is the temp displayed on the boiler). |
| mode           | Current operating mode of the boiler (see Modes below). |

I didn't bother looking into all these night temperature or vacation settings. If you intend to control the boiler via MQTT, you can probably implement such features yourself.

![MQTT topics](mqtt.png?raw=true "MQTT topics")

### Boiler inputs

These are the topics you can use to set values:

| Topic           | Description |
| --------------- | ----------- |
| set_temperature | Configure target temperature (integer) |
| set_mode        | Configure operating mode of the boiler (see Modes below) |
| set_hdo_enabled | Configure whether HDO detection is enabled (0/1). Affects mode availability. |

### Modes

This is the list of recognized mode values. See the [original app](https://play.google.com/store/apps/details?id=cz.dzd.smartbojler&hl=cs&gl=US) for more details, but the names are self-explanatory.

| Value           | Notes                       |
| --------------- | --------------------------- |
| STOP            | cannot be set               |
| NORMAL          | can be set if hdo_enabled=0 |
| HDO             | can be set if hdo_enabled=1 |
| SMART           | can be set if hdo_enabled=0 |
| SMARTHDO        | can be set if hdo_enabled=1 |
| ANTIFROST       |                             |
| NIGHT           |                             |

Toggling `set_hdo_enabled` will also cause the mode to switch between NORMAL/HDO and SMART/SMARTHDO.

## Bugs

During my testing, I noticed the BLE stack on ESP32 isn't 100% stable. There's an elevated risk of the `ESP_GATTC_REG_FOR_NOTIFY_EVT` event not arriving, especially if there's an increased Wi-Fi activity at the same time. The consequence is that MQTT topic values don't get updated at startup, but as events seem to keep arriving anyway despite the glitch, it's not so critical after all.
