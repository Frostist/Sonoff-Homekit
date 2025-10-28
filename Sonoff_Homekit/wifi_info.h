#ifndef WIFI_INFO_H_
#define WIFI_INFO_H_

#include <Arduino.h>

#if defined(ESP8266)
#include <ESP8266WiFi.h>
#elif defined(ESP32)
#include <WiFi.h>
#endif

const char *ssid = "WIFI SSID";
const char *password = "WIFI Password";

// WiFi connection timeout and retry settings
const unsigned long WIFI_TIMEOUT = 30000;  // 30 seconds timeout
const unsigned long WIFI_RETRY_INTERVAL = 60000;  // 1 minute between retries
const unsigned long WIFI_CHECK_INTERVAL = 5000;  // Check connection every 5 seconds (not every loop)

unsigned long lastWifiRetry = 0;
unsigned long lastWifiCheck = 0;
bool wifiConnected = false;

void wifi_connect() {
	Serial.println("=== WiFi Connection Starting ===");
	
	WiFi.persistent(false);  // Don't save WiFi config to flash every time (reduces wear)
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(true);
	WiFi.begin(ssid, password);
	
	Serial.printf("Connecting to: %s\n", ssid);
	
	// Flash LED during WiFi connection
	unsigned long lastFlash = 0;
	unsigned long connectionStart = millis();
	bool ledState = false;
	
	while (!WiFi.isConnected() && (millis() - connectionStart) < WIFI_TIMEOUT) {
		delay(100);
		Serial.print(".");
		
		// Flash LED every 500ms during connection
		if (millis() - lastFlash >= 500) {
			ledState = !ledState;
			digitalWrite(13, ledState ? LOW : HIGH); // PIN_LED is 13
			lastFlash = millis();
		}
	}
	
	Serial.println(); // New line after dots
	
	if (WiFi.isConnected()) {
		// Turn off LED when WiFi connects
		digitalWrite(13, HIGH); // Turn off LED (HIGH = off for this LED)
		wifiConnected = true;
		lastWifiRetry = millis();
		lastWifiCheck = millis();
		
		Serial.println("WiFi connected successfully!");
		Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
		Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
		Serial.printf("Channel: %d\n", WiFi.channel());
	} else {
		// WiFi connection failed, will retry later
		digitalWrite(13, HIGH); // Turn off LED
		wifiConnected = false;
		lastWifiRetry = millis();
		Serial.println("WiFi connection failed - will retry in 60 seconds");
	}
}

// Function to check and maintain WiFi connection
// CRITICAL: Only check periodically (every 5 seconds), not every loop iteration
// This prevents excessive WiFi library calls and potential watchdog resets
// RETURNS: true if WiFi just reconnected, false otherwise
bool wifi_check_and_reconnect() {
	unsigned long currentTime = millis();
	
	// Only check WiFi status every 5 seconds, not every loop iteration
	if ((currentTime - lastWifiCheck) < WIFI_CHECK_INTERVAL) {
		return false;  // No reconnection occurred
	}
	
	lastWifiCheck = currentTime;
	
	// Check if WiFi is still connected
	bool currentlyConnected = WiFi.isConnected();
	
	// If we lost connection, log it
	if (wifiConnected && !currentlyConnected) {
		Serial.println("\nWARNING: WiFi connection lost!");
		Serial.printf("Last RSSI: %d dBm\n", WiFi.RSSI());
		wifiConnected = false;
	}
	
	// If WiFi is not connected and enough time has passed since last retry
	if (!currentlyConnected && (currentTime - lastWifiRetry) >= WIFI_RETRY_INTERVAL) {
		Serial.println("\n=== WiFi Reconnection Attempt ===");
		Serial.printf("Time since last attempt: %lu seconds\n", (currentTime - lastWifiRetry) / 1000);
		
		lastWifiRetry = currentTime;
		
		// Flash LED to indicate reconnection attempt (non-blocking)
		for (int i = 0; i < 3; i++) {
			digitalWrite(13, LOW);
			delay(100);
			digitalWrite(13, HIGH);
			delay(100);
		}
		
		// Disconnect cleanly before reconnecting
		Serial.println("Disconnecting...");
		WiFi.disconnect();
		delay(500);
		
		// Attempt reconnection
		Serial.println("Reconnecting...");
		WiFi.begin(ssid, password);
		
		// Wait for connection with timeout
		unsigned long connectionStart = millis();
		int dotCount = 0;
		
		while (!WiFi.isConnected() && (millis() - connectionStart) < WIFI_TIMEOUT) {
			delay(500);
			Serial.print(".");
			dotCount++;
			
			// Print status every 10 dots
			if (dotCount % 10 == 0) {
				Serial.printf(" [%d%%]\n", (int)((millis() - connectionStart) * 100 / WIFI_TIMEOUT));
			}
		}
		
		Serial.println();
		
		if (WiFi.isConnected()) {
			wifiConnected = true;
			digitalWrite(13, HIGH); // Turn off LED
			
			Serial.println("WiFi reconnected successfully!");
			Serial.printf("IP Address: %s\n", WiFi.localIP().toString().c_str());
			Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());
			Serial.printf("Channel: %d\n", WiFi.channel());
			
			return true;  // WiFi just reconnected!
		} else {
			wifiConnected = false;
			digitalWrite(13, HIGH); // Turn off LED
			
			Serial.println("WiFi reconnection failed");
			Serial.println("Will retry in 60 seconds");
			
			return false;  // Reconnection failed
		}
	}
	
	return false;  // No state change
}

// Function to get current WiFi connection status
bool is_wifi_connected() {
	return WiFi.isConnected();
}

// Function to get WiFi signal strength
int get_wifi_rssi() {
	return WiFi.RSSI();
}

// Function to get WiFi IP address as string
String get_wifi_ip() {
	if (WiFi.isConnected()) {
		return WiFi.localIP().toString();
	}
	return "Not Connected";
}

#endif /* WIFI_INFO_H_ */