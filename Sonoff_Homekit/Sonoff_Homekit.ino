#include <EEPROM.h>
#include <Arduino.h>
#include <arduino_homekit_server.h>
#include "wifi_info.h"

// Sonoff Basic GPIO pin definitions
#define PIN_SWITCH 12  // Relay pin
#define PIN_LED 13     // LED pin for green light
#define PIN_BUTTON 0   // Button pin

// Hardware version configuration
// Set this to true for ESP8285 v1.3 units (inverted relay logic)
// Set this to false for ESP8285 v1.0 units (normal relay logic)
// If your switch behavior is inverted, change this setting
#define ESP8285_V1_3 true

// Increased EEPROM size to accommodate HomeKit pairing data
#define EEPROM_SIZE 512
// Move switch state storage to avoid conflicts with HomeKit library
#define SWITCH_STATE_ADDRESS 450
#define LOG_D(fmt, ...) printf_P(PSTR(fmt "\n"), ##__VA_ARGS__);

// Button debouncing variables
unsigned long lastButtonPress = 0;
const unsigned long buttonDebounceTime = 200;  // 200ms debounce
bool lastButtonState = HIGH;
bool buttonPressed = false;

// Button hold variables for EEPROM wipe
unsigned long buttonHoldStart = 0;
const unsigned long buttonHoldTime = 7000;  // 7 seconds
bool buttonHeld = false;

// access your HomeKit characteristics defined in my_accessory.c
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t cha_switch_on;

void setup() {
  // Initialize EEPROM with larger size for HomeKit pairing data
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize LED pin for WiFi connection indicator
  pinMode(PIN_LED, OUTPUT);
  // digitalWrite(PIN_LED, HIGH); // Start with LED off

  // Initialize button pin with internal pull-up
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  //Connect to wifi
  Serial.begin(115200);
  wifi_connect();

  // Start HomeKit setup first to ensure proper EEPROM initialization
  Serial.println("Initializing HomeKit...");
  my_homekit_setup();
  Serial.println("HomeKit initialized successfully");

  // Read switch state from EEPROM (moved to safer address)
  bool switchOn = EEPROM.read(SWITCH_STATE_ADDRESS);
  
  // Check if EEPROM is uninitialized (0x00) and default to OFF
  if (switchOn == 0x00) {
    switchOn = false;  // Default to OFF
    EEPROM.write(SWITCH_STATE_ADDRESS, false);
    EEPROM.commit();
  }

//Print and change state
#if ESP8285_V1_3
  digitalWrite(PIN_SWITCH, switchOn ? HIGH : LOW);  // Inverted logic for v1.3
#else
  digitalWrite(PIN_SWITCH, switchOn ? LOW : HIGH);  // Normal logic for v1.0
#endif
  Serial.println("Switch on: ");
  Serial.println(switchOn);
  
  // Synchronize HomeKit characteristic with EEPROM value
  cha_switch_on.value.bool_value = switchOn;
}

void loop() {
  // Check and maintain WiFi connection
  wifi_check_and_reconnect();
  
  my_homekit_loop();

  // Handle button press
  handleButtonPress();

  delay(10);
}

// Handle button press with debouncing and hold detection
void handleButtonPress() {
  bool currentButtonState = digitalRead(PIN_BUTTON);

  // Check if button state changed and debounce time has passed
  if (currentButtonState != lastButtonState && (millis() - lastButtonPress) > buttonDebounceTime) {

    // Button was pressed (LOW because of pull-up)
    if (currentButtonState == LOW) {
      buttonPressed = true;
      buttonHoldStart = millis();
      buttonHeld = false;
      Serial.println("Button pressed!");
    }

    // Button was released
    if (currentButtonState == HIGH) {
      // Only toggle relay if button wasn't held for EEPROM wipe
      if (buttonPressed && !buttonHeld) {
        toggleRelay();
      }
      buttonPressed = false;
      buttonHeld = false;
    }

    lastButtonState = currentButtonState;
    lastButtonPress = millis();
  }

  // Check for button hold duration
  if (currentButtonState == LOW && buttonPressed && !buttonHeld) {
    if (millis() - buttonHoldStart >= buttonHoldTime) {
      buttonHeld = true;
      wipeEEPROM();
    }
  }
}

// Toggle relay state
void toggleRelay() {
  bool currentState = cha_switch_on.value.bool_value;
  bool newState = !currentState;

  // Update HomeKit characteristic
  cha_switch_on.value.bool_value = newState;

// Update physical relay and LED
#if ESP8285_V1_3
  digitalWrite(PIN_SWITCH, newState ? HIGH : LOW);  // Inverted logic for v1.3
#else
  digitalWrite(PIN_SWITCH, newState ? LOW : HIGH);  // Normal logic for v1.0
#endif

  // Save to EEPROM (using new address)
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(SWITCH_STATE_ADDRESS, newState);
  EEPROM.commit();

  // Notify HomeKit of the change
  homekit_characteristic_notify(&cha_switch_on, cha_switch_on.value);

  Serial.print("Relay toggled to: ");
  Serial.println(newState ? "ON" : "OFF");
}

// Wipe EEPROM memory for factory reset
void wipeEEPROM() {
  Serial.println("Button held for 7 seconds - performing factory reset...");
  
  // Flash LED rapidly to indicate factory reset
  for (int i = 0; i < 10; i++) {
    digitalWrite(PIN_LED, LOW);
    delay(100);
    digitalWrite(PIN_LED, HIGH);
    delay(100);
  }
  
  // Wipe EEPROM with zeros (more reliable than 0xFF for HomeKit)
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0x00);  // Use 0x00 instead of 0xFF
  }
  EEPROM.commit();
  
  Serial.println("EEPROM wiped successfully!");
  Serial.println("HomeKit pairing data cleared!");
  Serial.println("Device will restart in 5 seconds...");
  
  // Longer delay to ensure EEPROM is fully written
  delay(5000);
  ESP.restart();
}

//==============================
// HomeKit setup and loop
//==============================

static uint32_t next_heap_millis = 0;

//Called when the switch value is changed by iOS Home APP
void cha_switch_on_setter(const homekit_value_t value) {
  bool on = value.bool_value;
  cha_switch_on.value.bool_value = on;  //sync the value
  LOG_D("Switch: %s", on ? "ON" : "OFF");
#if ESP8285_V1_3
  digitalWrite(PIN_SWITCH, on ? HIGH : LOW);  // Inverted logic for v1.3
#else
  digitalWrite(PIN_SWITCH, on ? LOW : HIGH);        // Normal logic for v1.0
#endif

  //Write to Memory (using new address)
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.write(SWITCH_STATE_ADDRESS, on);
  EEPROM.commit();

  //Print state
  Serial.println("Write memory: ");
  Serial.println(on);
}

void my_homekit_setup() {
  pinMode(PIN_SWITCH, OUTPUT);
  // LED pin already initialized in setup()

  cha_switch_on.setter = cha_switch_on_setter;
  arduino_homekit_setup(&config);
}

void my_homekit_loop() {
  arduino_homekit_loop();
  const uint32_t t = millis();
  if (t > next_heap_millis) {
    // show heap info every 5 seconds
    next_heap_millis = t + 5 * 1000;
    LOG_D("Free heap: %d, HomeKit clients: %d",
          ESP.getFreeHeap(), arduino_homekit_connected_clients_count());
  }
}
