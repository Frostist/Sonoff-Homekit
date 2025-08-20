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
unsigned long lastWifiRetry = 0;
bool wifiConnected = false;

void wifi_connect() {
	WiFi.persistent(true);
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(true);
	WiFi.begin(ssid, password);
	Serial.println("WiFi connecting...");
	
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
	
	if (WiFi.isConnected()) {
		// Turn off LED when WiFi connects
		digitalWrite(13, HIGH); // Turn off LED (HIGH = off for this LED)
		wifiConnected = true;
		lastWifiRetry = millis();
		Serial.print("\n");
		Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
	} else {
		// WiFi connection failed, will retry later
		digitalWrite(13, HIGH); // Turn off LED
		wifiConnected = false;
		Serial.println("\nWiFi connection failed - will retry later");
	}
}

// Function to check and maintain WiFi connection
void wifi_check_and_reconnect() {
	unsigned long currentTime = millis();
	
	// If WiFi is not connected and enough time has passed since last retry
	if (!WiFi.isConnected() && (currentTime - lastWifiRetry) >= WIFI_RETRY_INTERVAL) {
		Serial.println("WiFi disconnected - attempting to reconnect...");
		lastWifiRetry = currentTime;
		
		// Flash LED to indicate reconnection attempt
		for (int i = 0; i < 3; i++) {
			digitalWrite(13, LOW);
			delay(200);
			digitalWrite(13, HIGH);
			delay(200);
		}
		
		// Try to reconnect
		WiFi.disconnect();
		delay(1000);
		WiFi.begin(ssid, password);
		
		// Wait for connection with timeout
		unsigned long connectionStart = millis();
		while (!WiFi.isConnected() && (millis() - connectionStart) < WIFI_TIMEOUT) {
			delay(100);
			Serial.print(".");
		}
		
		if (WiFi.isConnected()) {
			wifiConnected = true;
			digitalWrite(13, HIGH); // Turn off LED
			Serial.print("\n");
			Serial.printf("WiFi reconnected, IP: %s\n", WiFi.localIP().toString().c_str());
		} else {
			wifiConnected = false;
			digitalWrite(13, HIGH); // Turn off LED
			Serial.println("\nWiFi reconnection failed - will retry later");
		}
	}
}

// Function to get current WiFi connection status
bool is_wifi_connected() {
	return WiFi.isConnected();
}

#endif /* WIFI_INFO_H_ */
