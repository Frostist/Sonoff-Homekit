# Sonoff HomeKit Switch

A project that you use the Arduino IDE and load onto a ESP8285/ESP32 Sonoff R2 and it allows you to control it natively with Apple HomeKit.

Go back to [Will's Homekit stuff](https://github.com/Frostist/Wills-Homekit-Stuff)

## I am still playing around with a lot of the WiFi & EEPROM code, due to the fact that i have encoutered many issues.
- Frequent lights toggling randomly
- If WiFi router reboots, lights disconnect and don't reconnect until rebooted
- Talk to each other and seem to toggle each other on and off

## Notes
- Pin 2 controls a relay that allows for the fan to change states from on to off
- This is for a on/off state switch (No dimming)

### How to Program
1. Take Sonoff Unit apart (find the 4 pins on the board for 3v, ground Tx & Rx
2. Plug in your USB UART Cable
3. Have the Ardunio IDE installed
4. Download [Ardunio HomeKit Library](https://github.com/Mixiaoxiao/Arduino-HomeKit-ESP8266/) and ESP8266 [Board Manager](https://arduino-esp8266.readthedocs.io/en/3.1.2/installing.html)
5. You might also need [ESP HomeKit Library](https://github.com/maximkulkin/esp-homekit) / [ESP Serial Library](https://github.com/plerup/espsoftwareserial/)
6. Download this file and then open the .ino file with Ardunio




## Hardware Version Compatibility

### ESP8285 v1.0 vs v1.3 Units
If you experience inverted switch behavior (ON becomes OFF and vice versa) between different ESP8285 hardware versions:

1. **For ESP8285 v1.0 units**: Set `#define ESP8285_V1_3 false` in the code
2. **For ESP8285 v1.3 units**: Set `#define ESP8285_V1_3 true` in the code

The difference is due to hardware changes in relay wiring or GPIO pin behavior between versions. The code now includes conditional compilation to handle both hardware versions.
