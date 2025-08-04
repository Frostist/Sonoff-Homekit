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

void wifi_connect() {
	WiFi.persistent(true);
	WiFi.mode(WIFI_STA);
	WiFi.setAutoReconnect(true);
	WiFi.begin(ssid, password);
	Serial.println("WiFi connecting...");
	
	// Flash LED during WiFi connection
	unsigned long lastFlash = 0;
	bool ledState = false;
	
	while (!WiFi.isConnected()) {
		delay(100);
		Serial.print(".");
		
		// Flash LED every 500ms during connection
		if (millis() - lastFlash >= 500) {
			ledState = !ledState;
			digitalWrite(13, ledState ? LOW : HIGH); // PIN_LED is 13
			lastFlash = millis();
		}
	}
	
	// Turn off LED when WiFi connects
	digitalWrite(13, HIGH); // Turn off LED (HIGH = off for this LED)
	
	Serial.print("\n");
	Serial.printf("WiFi connected, IP: %s\n", WiFi.localIP().toString().c_str());
}

#endif /* WIFI_INFO_H_ */
