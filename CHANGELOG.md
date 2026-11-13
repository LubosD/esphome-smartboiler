# Changelog

## v2.0.0 (unreleased)

### Breaking changes

Your YAML needs to be updated - see [Upgrading from older versions](README.md#upgrading-from-older-versions). ESPHome shows a warning with a link to these notes when it finds the old configuration.

* `smartboiler:` is a single mapping instead of a list. A list with one item is still accepted (with a warning), only one water heater per ESP is supported.
* The *Monitoring* switch is created by the component, remove the `switch:` with `platform: ble_client`.
* All entities are created automatically with default names. Entity names in YAML are optional.
* The pairing UID is stored under a key derived from the water heater MAC address. It is migrated automatically when the new version is flashed with the existing entity names for the first time.

### New entities

* Status: low tariff, problem, error, model, capacity, PROG temperature.
* Controls: monitoring, HDO decoder and anti-legionella switches, PROG low/high temperature, holiday start/length, reset energy button.

### Fixes

* Unconfirmed requests are re-sent after 10 s and dropped after 30 s, rejected requests are handled.
* Authentication is retried until the water heater responds.
* Firmware version is requested on connect.
* STOP, TEST and HOLIDAY modes are reported correctly, thermostat shows OFF in STOP mode.
* No crash when optional entities are not configured, values from the water heater are validated.
* Water heater clock is not adjusted twice while an adjustment is pending.

## v1.0.0

Last version with the original configuration format (list of water heaters, explicit entity names, `switch` with `platform: ble_client`).
