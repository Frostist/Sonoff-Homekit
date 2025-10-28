#include <EEPROM.h>
#include <Arduino.h>
#include <arduino_homekit_server.h>
#include "wifi_info.h"

// Sonoff Basic GPIO pin definitions
#define PIN_SWITCH 12  // Relay pin
#define PIN_LED 13     // LED pin
#define PIN_BUTTON 0   // Button pin

// Hardware version configuration
#define ESP8285_V1_3 true

// EEPROM configuration
// HomeKit library uses addresses 0-1408, we use 1409+ for switch state
#define SWITCH_STATE_ADDRESS 1409
#define EEPROM_MAGIC_ADDRESS 1410
#define EEPROM_MAGIC_VALUE 0xAB
#define LOG_D(fmt, ...) printf_P(PSTR(fmt "\n"), ##__VA_ARGS__);

// Button debouncing
unsigned long lastButtonPress = 0;
const unsigned long buttonDebounceTime = 200;
bool lastButtonState = HIGH;
bool buttonPressed = false;

// Button hold for factory reset
unsigned long buttonHoldStart = 0;
const unsigned long buttonHoldTime = 7000;
bool buttonHeld = false;

// Heap monitoring
unsigned long lastHeapCheck = 0;
const unsigned long heapCheckInterval = 10000;

// HomeKit client monitoring for auto-recovery
unsigned long lastClientCheckTime = 0;
unsigned long noClientStartTime = 0;
const unsigned long NO_CLIENT_TIMEOUT = 120000;  // 2 minutes
bool hadClientBefore = false;

// Access HomeKit
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t cha_switch_on;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Sonoff HomeKit v3.0 ===");
  
  // CRITICAL: Don't call EEPROM.begin() before HomeKit initialization!
  // The HomeKit library calls EEPROM.begin() internally and manages addresses 0-1408
  // If we call it first, we might reinitialize and clear HomeKit's pairing data
  
  // Initialize pins
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SWITCH, OUTPUT);

  // Connect to WiFi
  wifi_connect();

  // Initialize HomeKit FIRST - it will initialize EEPROM internally
  Serial.println("Initializing HomeKit...");
  my_homekit_setup();
  Serial.println("HomeKit initialized");

  // Now we can safely read/write EEPROM for our switch state (1409+)
  uint8_t magicValue = EEPROM.read(EEPROM_MAGIC_ADDRESS);
  bool eepromInitialized = (magicValue == EEPROM_MAGIC_VALUE);
  
  bool switchOn = false;
  
  if (!eepromInitialized) {
    Serial.println("First boot - initializing switch state");
    switchOn = false;
    EEPROM.write(SWITCH_STATE_ADDRESS, 0x00);
    EEPROM.write(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC_VALUE);
    EEPROM.commit();
  } else {
    uint8_t storedState = EEPROM.read(SWITCH_STATE_ADDRESS);
    if (storedState == 0x01) {
      switchOn = true;
    } else if (storedState == 0x00) {
      switchOn = false;
    } else {
      Serial.printf("Warning: Corrupted state 0x%02X\n", storedState);
      switchOn = false;
      EEPROM.write(SWITCH_STATE_ADDRESS, 0x00);
      EEPROM.commit();
    }
  }

  // Set relay to restored state
  #if ESP8285_V1_3
    digitalWrite(PIN_SWITCH, switchOn ? HIGH : LOW);
  #else
    digitalWrite(PIN_SWITCH, switchOn ? LOW : HIGH);
  #endif
  
  Serial.printf("Switch: %s\n", switchOn ? "ON" : "OFF");
  cha_switch_on.value.bool_value = switchOn;
  
  Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=== Setup Complete ===\n");
}

void loop() {
  // WiFi maintenance
  wifi_check_and_reconnect();
  
  // HomeKit loop
  my_homekit_loop();

  // Button handling
  handleButtonPress();

  // AUTO-RECOVERY: Restart if no HomeKit clients for 2 minutes
  unsigned long currentTime = millis();
  
  if (currentTime - lastClientCheckTime >= 10000) {
    lastClientCheckTime = currentTime;
    
    int clientCount = arduino_homekit_connected_clients_count();
    
    if (clientCount > 0) {
      hadClientBefore = true;
      noClientStartTime = 0;
    }
    else if (WiFi.isConnected()) {
      if (noClientStartTime == 0) {
        noClientStartTime = currentTime;
      }
      else if (hadClientBefore && (currentTime - noClientStartTime >= NO_CLIENT_TIMEOUT)) {
        Serial.println("\n⚠️  No HomeKit clients for 2 minutes - Restarting");
        Serial.println("Restarting in 3 seconds...\n");
        delay(3000);
        ESP.restart();
      }
    }
  }

  // Heap monitoring
  if (millis() - lastHeapCheck >= heapCheckInterval) {
    lastHeapCheck = millis();
    uint32_t freeHeap = ESP.getFreeHeap();
    int clients = arduino_homekit_connected_clients_count();
    
    Serial.printf("Heap: %d bytes, Clients: %d", freeHeap, clients);
    
    if (freeHeap < 8000) {
      Serial.print(" ⚠️ LOW MEM");
    }
    
    // Show countdown if waiting for clients
    if (hadClientBefore && noClientStartTime > 0 && WiFi.isConnected()) {
      unsigned long timeWithoutClients = (millis() - noClientStartTime) / 1000;
      unsigned long timeUntilRestart = (NO_CLIENT_TIMEOUT / 1000) - timeWithoutClients;
      Serial.printf(" | No clients: %lus (restart: %lus)", timeWithoutClients, timeUntilRestart);
    }
    
    Serial.println();
  }

  delay(10);
}

void handleButtonPress() {
  bool currentButtonState = digitalRead(PIN_BUTTON);

  if (currentButtonState != lastButtonState && (millis() - lastButtonPress) > buttonDebounceTime) {
    if (currentButtonState == LOW) {
      buttonPressed = true;
      buttonHoldStart = millis();
      buttonHeld = false;
      Serial.println("Button pressed");
    }

    if (currentButtonState == HIGH) {
      if (buttonPressed && !buttonHeld) {
        toggleRelay();
      }
      buttonPressed = false;
      buttonHeld = false;
    }

    lastButtonState = currentButtonState;
    lastButtonPress = millis();
  }

  if (currentButtonState == LOW && buttonPressed && !buttonHeld) {
    if (millis() - buttonHoldStart >= buttonHoldTime) {
      buttonHeld = true;
      wipeEEPROM();
    }
  }
}

void toggleRelay() {
  bool currentState = cha_switch_on.value.bool_value;
  bool newState = !currentState;

  Serial.printf("Toggle: %s -> %s\n", currentState ? "ON" : "OFF", newState ? "ON" : "OFF");

  cha_switch_on.value.bool_value = newState;

  #if ESP8285_V1_3
    digitalWrite(PIN_SWITCH, newState ? HIGH : LOW);
  #else
    digitalWrite(PIN_SWITCH, newState ? LOW : HIGH);
  #endif

  EEPROM.write(SWITCH_STATE_ADDRESS, newState ? 0x01 : 0x00);
  if (EEPROM.commit()) {
    Serial.println("State saved");
  } else {
    Serial.println("ERROR: Save failed");
  }

  homekit_characteristic_notify(&cha_switch_on, cha_switch_on.value);
}

void wipeEEPROM() {
  Serial.println("\n=== FACTORY RESET ===");
  Serial.println("Hold confirmed - wiping all data...");
  
  // Flash LED rapidly to indicate factory reset
  for (int i = 0; i < 20; i++) {
    digitalWrite(PIN_LED, LOW);
    delay(100);
    digitalWrite(PIN_LED, HIGH);
    delay(100);
  }
  
  Serial.println("Erasing EEPROM...");
  
  // Wipe entire EEPROM (including HomeKit pairing at 0-1408)
  for (int i = 0; i < 4096; i++) {
    EEPROM.write(i, 0x00);
    if (i % 512 == 0 && i > 0) {
      Serial.printf("Progress: %d/4096\n", i);
    }
  }
  
  if (EEPROM.commit()) {
    Serial.println("✅ EEPROM wiped!");
  } else {
    Serial.println("❌ Commit failed!");
  }
  
  Serial.println("All pairing data cleared");
  Serial.println("Restarting in 3 seconds...\n");
  delay(3000);
  ESP.restart();
}

//==============================
// HomeKit
//==============================

static uint32_t next_heap_millis = 0;

void cha_switch_on_setter(const homekit_value_t value) {
  bool on = value.bool_value;
  cha_switch_on.value.bool_value = on;
  
  LOG_D("HomeKit: %s", on ? "ON" : "OFF");
  
  #if ESP8285_V1_3
    digitalWrite(PIN_SWITCH, on ? HIGH : LOW);
  #else
    digitalWrite(PIN_SWITCH, on ? LOW : HIGH);
  #endif

  EEPROM.write(SWITCH_STATE_ADDRESS, on ? 0x01 : 0x00);
  if (EEPROM.commit()) {
    Serial.printf("HomeKit: %s\n", on ? "ON" : "OFF");
  } else {
    Serial.println("ERROR: Save failed");
  }
}

void my_homekit_setup() {
  cha_switch_on.setter = cha_switch_on_setter;
  arduino_homekit_setup(&config);
}

void my_homekit_loop() {
  arduino_homekit_loop();
  const uint32_t t = millis();
  if (t > next_heap_millis) {
    next_heap_millis = t + 30 * 1000;
    LOG_D("Free heap: %d, HomeKit clients: %d",
          ESP.getFreeHeap(), arduino_homekit_connected_clients_count());
  }
}