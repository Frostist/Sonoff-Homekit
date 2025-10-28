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
#define EEPROM_SIZE 4096
// Move switch state storage to avoid conflicts with HomeKit library
// HomeKit uses addresses 0-1408, so we use 1409 for switch state
#define SWITCH_STATE_ADDRESS 1409
// Magic number to detect if EEPROM has been initialized
#define EEPROM_MAGIC_ADDRESS 1410
#define EEPROM_MAGIC_VALUE 0xAB
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

// Heap monitoring for debugging
unsigned long lastHeapCheck = 0;
const unsigned long heapCheckInterval = 10000;  // Check every 10 seconds

// access your HomeKit characteristics defined in my_accessory.c
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t cha_switch_on;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Sonoff HomeKit Starting ===");
  
  // Initialize EEPROM with larger size for HomeKit pairing data
  // CRITICAL: Only call EEPROM.begin() ONCE to avoid multiple RAM allocations
  EEPROM.begin(EEPROM_SIZE);
  Serial.println("EEPROM initialized");
  
  // Initialize LED pin for WiFi connection indicator
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH); // Start with LED off (HIGH = off for this LED)

  // Initialize button pin with internal pull-up
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Connect to WiFi
  wifi_connect();

  // Start HomeKit setup first to ensure proper EEPROM initialization
  Serial.println("Initializing HomeKit...");
  my_homekit_setup();
  Serial.println("HomeKit initialized successfully");

  // Check if EEPROM has been initialized with our magic number
  uint8_t magicValue = EEPROM.read(EEPROM_MAGIC_ADDRESS);
  bool eepromInitialized = (magicValue == EEPROM_MAGIC_VALUE);
  
  bool switchOn = false;
  
  if (!eepromInitialized) {
    // First boot - initialize EEPROM
    Serial.println("First boot detected - initializing EEPROM");
    switchOn = false;  // Default to OFF
    EEPROM.write(SWITCH_STATE_ADDRESS, 0x00);
    EEPROM.write(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC_VALUE);
    EEPROM.commit();
    Serial.println("EEPROM initialized to default state (OFF)");
  } else {
    // Read switch state from EEPROM
    uint8_t storedState = EEPROM.read(SWITCH_STATE_ADDRESS);
    
    // Validate the stored state (should be 0x00 or 0x01)
    if (storedState == 0x01) {
      switchOn = true;
    } else if (storedState == 0x00) {
      switchOn = false;
    } else {
      // Corrupted value - default to OFF and fix it
      Serial.printf("Warning: Corrupted EEPROM value: 0x%02X - resetting to OFF\n", storedState);
      switchOn = false;
      EEPROM.write(SWITCH_STATE_ADDRESS, 0x00);
      EEPROM.commit();
    }
  }

  // Set physical relay state
  #if ESP8285_V1_3
    digitalWrite(PIN_SWITCH, switchOn ? HIGH : LOW);  // Inverted logic for v1.3
  #else
    digitalWrite(PIN_SWITCH, switchOn ? LOW : HIGH);  // Normal logic for v1.0
  #endif
  
  Serial.printf("Switch initialized to: %s\n", switchOn ? "ON" : "OFF");
  
  // Synchronize HomeKit characteristic with EEPROM value
  cha_switch_on.value.bool_value = switchOn;
  
  // Report initial heap status
  Serial.printf("Initial free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=== Setup Complete ===\n");
}

void loop() {
  // Check and maintain WiFi connection
  // Returns true if WiFi just reconnected
  bool justReconnected = wifi_check_and_reconnect();
  
  // If WiFi just reconnected, force HomeKit to restart mDNS
  if (justReconnected) {
    Serial.println("WiFi reconnected - restarting HomeKit services...");
    // Give WiFi a moment to stabilize
    delay(1000);
    // Force mDNS re-announcement
    homekit_mdns_restart();
    Serial.println("HomeKit services restarted - device should be discoverable");
  }
  
  // Run HomeKit loop
  my_homekit_loop();

  // Handle button press
  handleButtonPress();

  // Periodic heap monitoring for debugging memory issues
  if (millis() - lastHeapCheck >= heapCheckInterval) {
    lastHeapCheck = millis();
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("Free heap: %d bytes", freeHeap);
    
    // Warn if heap is getting dangerously low
    if (freeHeap < 8000) {
      Serial.print(" - WARNING: Low memory!");
    }
    Serial.println();
  }

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
      Serial.println("Button pressed");
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

  Serial.printf("Toggle relay: %s -> %s\n", currentState ? "ON" : "OFF", newState ? "ON" : "OFF");

  // Update HomeKit characteristic
  cha_switch_on.value.bool_value = newState;

  // Update physical relay
  #if ESP8285_V1_3
    digitalWrite(PIN_SWITCH, newState ? HIGH : LOW);  // Inverted logic for v1.3
  #else
    digitalWrite(PIN_SWITCH, newState ? LOW : HIGH);  // Normal logic for v1.0
  #endif

  // Save to EEPROM - write explicit values for clarity
  // CRITICAL: Do NOT call EEPROM.begin() again - it's already initialized in setup()
  EEPROM.write(SWITCH_STATE_ADDRESS, newState ? 0x01 : 0x00);
  if (EEPROM.commit()) {
    Serial.println("State saved to EEPROM successfully");
  } else {
    Serial.println("ERROR: Failed to save state to EEPROM");
  }

  // Notify HomeKit of the change
  homekit_characteristic_notify(&cha_switch_on, cha_switch_on.value);
}

// Wipe EEPROM memory for factory reset
void wipeEEPROM() {
  Serial.println("\n=== FACTORY RESET INITIATED ===");
  Serial.println("Button held for 7 seconds - performing factory reset...");
  
  // Flash LED rapidly to indicate factory reset
  for (int i = 0; i < 20; i++) {
    digitalWrite(PIN_LED, LOW);
    delay(100);
    digitalWrite(PIN_LED, HIGH);
    delay(100);
  }
  
  Serial.println("Wiping EEPROM...");
  
  // Wipe EEPROM with zeros (more reliable than 0xFF for HomeKit)
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0x00);
    
    // Show progress every 512 bytes
    if (i % 512 == 0 && i > 0) {
      Serial.printf("Progress: %d/%d bytes\n", i, EEPROM_SIZE);
    }
  }
  
  if (EEPROM.commit()) {
    Serial.println("EEPROM wiped successfully!");
  } else {
    Serial.println("ERROR: EEPROM commit failed!");
  }
  
  Serial.println("HomeKit pairing data cleared!");
  Serial.println("WiFi settings cleared!");
  Serial.println("Device will restart in 5 seconds...");
  
  // Longer delay to ensure EEPROM is fully written
  delay(5000);
  
  Serial.println("Restarting now...");
  ESP.restart();
}

//==============================
// HomeKit setup and loop
//==============================

static uint32_t next_heap_millis = 0;

// Function to help HomeKit recover after WiFi reconnection
void homekit_mdns_restart() {
  // The HomeKit library manages mDNS internally, but we can help by
  // ensuring the network stack is fully ready before HomeKit tries to use it
  Serial.println("Ensuring mDNS is properly announced...");
  
  // Multiple HomeKit loop iterations help ensure mDNS gets properly announced
  for (int i = 0; i < 5; i++) {
    arduino_homekit_loop();
    delay(100);
  }
  
  Serial.println("mDNS re-announcement complete");
}

// Called when the switch value is changed by iOS Home APP
void cha_switch_on_setter(const homekit_value_t value) {
  bool on = value.bool_value;
  cha_switch_on.value.bool_value = on;  // sync the value
  
  LOG_D("HomeKit command: Switch %s", on ? "ON" : "OFF");
  
  // Update physical relay
  #if ESP8285_V1_3
    digitalWrite(PIN_SWITCH, on ? HIGH : LOW);  // Inverted logic for v1.3
  #else
    digitalWrite(PIN_SWITCH, on ? LOW : HIGH);  // Normal logic for v1.0
  #endif

  // Save to EEPROM - write explicit values for clarity
  // CRITICAL: Do NOT call EEPROM.begin() again - it's already initialized in setup()
  EEPROM.write(SWITCH_STATE_ADDRESS, on ? 0x01 : 0x00);
  if (EEPROM.commit()) {
    Serial.printf("HomeKit state saved: %s\n", on ? "ON" : "OFF");
  } else {
    Serial.println("ERROR: Failed to save HomeKit state to EEPROM");
  }
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
    // show heap info every 30 seconds (reduced from 5 to minimize serial spam)
    next_heap_millis = t + 30 * 1000;
    LOG_D("Free heap: %d, HomeKit clients: %d",
          ESP.getFreeHeap(), arduino_homekit_connected_clients_count());
  }
}