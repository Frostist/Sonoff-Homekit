#include <EEPROM.h>
#include <Arduino.h>
#include <arduino_homekit_server.h>
#include "wifi_info.h"

// Sonoff Basic GPIO pin definitions
#define PIN_SWITCH 12
#define PIN_LED 13
#define PIN_BUTTON 0

#define ESP8285_V1_3 true

// EEPROM - HomeKit uses 0-1408, we use 1409+
#define SWITCH_STATE_ADDRESS 1409
#define EEPROM_MAGIC_ADDRESS 1410
#define EEPROM_MAGIC_VALUE 0xAB
#define LOG_D(fmt, ...) printf_P(PSTR(fmt "\n"), ##__VA_ARGS__);

// Button variables
unsigned long lastButtonPress = 0;
const unsigned long buttonDebounceTime = 200;
bool lastButtonState = HIGH;
bool buttonPressed = false;
unsigned long buttonHoldStart = 0;
const unsigned long buttonHoldTime = 7000;
bool buttonHeld = false;

// Monitoring
unsigned long lastHeapCheck = 0;
const unsigned long heapCheckInterval = 10000;

// HomeKit client monitoring for auto-recovery
unsigned long lastClientCheckTime = 0;
unsigned long noClientStartTime = 0;
const unsigned long NO_CLIENT_TIMEOUT = 120000;  // 2 minutes
bool hadClientBefore = false;

// HomeKit
extern "C" homekit_server_config_t config;
extern "C" homekit_characteristic_t cha_switch_on;

void setup() {
  Serial.begin(115200);
  Serial.println("\n\n=== Sonoff HomeKit v3.1 ===");
  
  // Initialize pins
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_SWITCH, OUTPUT);

  // Connect WiFi
  wifi_connect();

  // Initialize HomeKit FIRST
  Serial.println("Initializing HomeKit...");
  my_homekit_setup();
  Serial.println("HomeKit initialized");

  // Restore switch state
  uint8_t magicValue = EEPROM.read(EEPROM_MAGIC_ADDRESS);
  bool eepromInitialized = (magicValue == EEPROM_MAGIC_VALUE);
  
  bool switchOn = false;
  
  if (!eepromInitialized) {
    Serial.println("First boot - init switch state");
    switchOn = false;
    EEPROM.write(SWITCH_STATE_ADDRESS, 0x00);
    EEPROM.write(EEPROM_MAGIC_ADDRESS, EEPROM_MAGIC_VALUE);
    EEPROM.commit();
  } else {
    uint8_t storedState = EEPROM.read(SWITCH_STATE_ADDRESS);
    switchOn = (storedState == 0x01);
  }

  // Set relay
  #if ESP8285_V1_3
    digitalWrite(PIN_SWITCH, switchOn ? HIGH : LOW);
  #else
    digitalWrite(PIN_SWITCH, switchOn ? LOW : HIGH);
  #endif
  
  Serial.printf("Switch: %s\n", switchOn ? "ON" : "OFF");
  cha_switch_on.value.bool_value = switchOn;
  
  Serial.printf("Heap: %d bytes\n", ESP.getFreeHeap());
  Serial.println("=== Ready ===\n");
}

void loop() {
  wifi_check_and_reconnect();
  my_homekit_loop();
  handleButtonPress();

  // AUTO-RECOVERY: Restart if no clients for 2 minutes
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
        Serial.println("\n⚠️  No clients for 2min - Restarting");
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
    
    Serial.printf("Heap: %d, Clients: %d", freeHeap, clients);
    
    if (freeHeap < 8000) Serial.print(" ⚠️");
    
    if (hadClientBefore && noClientStartTime > 0 && WiFi.isConnected()) {
      unsigned long waited = (millis() - noClientStartTime) / 1000;
      unsigned long remaining = (NO_CLIENT_TIMEOUT / 1000) - waited;
      Serial.printf(" | Wait: %lus, Restart: %lus", waited, remaining);
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
      if (buttonPressed && !buttonHeld) toggleRelay();
      buttonPressed = false;
      buttonHeld = false;
    }

    lastButtonState = currentButtonState;
    lastButtonPress = millis();
  }

  if (currentButtonState == LOW && buttonPressed && !buttonHeld) {
    if (millis() - buttonHoldStart >= buttonHoldTime) {
      buttonHeld = true;
      factoryReset();
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
  EEPROM.commit();
  
  homekit_characteristic_notify(&cha_switch_on, cha_switch_on.value);
}

void factoryReset() {
  Serial.println("\n=== FACTORY RESET ===");
  
  // Flash LED
  for (int i = 0; i < 20; i++) {
    digitalWrite(PIN_LED, LOW);
    delay(100);
    digitalWrite(PIN_LED, HIGH);
    delay(100);
  }
  
  // Use HomeKit's built-in storage reset (much more reliable than manual EEPROM wipe)
  Serial.println("Clearing HomeKit pairing data...");
  homekit_storage_reset();
  
  Serial.println("✅ Reset complete!");
  Serial.println("Restarting in 3s...\n");
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
  EEPROM.commit();
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
