# Sonoff_Homekit

A simple project that allows for a fan to be controlled by an ESP8285 unit.

Go back to [Will's Homekit stuff](https://github.com/Frostist/Wills-Homekit-Stuff)

# This is a work in progress, I am hoping to have it finished soon with pictures and videos to help!

## Notes
- Pin 2 controls a relay that allows for the fan to change states from on to off
- This is for a on/off state fan


### NB: This project requires the use of the units EMPROM.
- THis might mean depending on the unit you are using that you might have to change where the data is stored.
- The reason for this... We have loadshedding and without this addon, my fan would switch off from lack of power and then remain off even if the power comes back on.

## Hardware Version Compatibility

### ESP8285 v1.0 vs v1.3 Units
If you experience inverted switch behavior (ON becomes OFF and vice versa) between different ESP8285 hardware versions:

1. **For ESP8285 v1.0 units**: Set `#define ESP8285_V1_3 false` in the code
2. **For ESP8285 v1.3 units**: Set `#define ESP8285_V1_3 true` in the code

The difference is due to hardware changes in relay wiring or GPIO pin behavior between versions. The code now includes conditional compilation to handle both hardware versions.
