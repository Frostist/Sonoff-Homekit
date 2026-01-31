# Updates




v3.1 - Date: 28 Oct 2025
- Simplified EEPROM state handling for the switch, reducing edge-case branches.
- Cleaned up logging labels and output formatting for heap and client monitoring.
- Replaced manual EEPROM wipe with a HomeKit storage reset in the factory reset flow.

v3.2 - Date: 31 Jan 2026
- Reboot after two failed WiFi reconnection attempts to avoid endless search loops.
- Factory reset now erases WiFi config via `ESP.eraseConfig()` after HomeKit reset.
