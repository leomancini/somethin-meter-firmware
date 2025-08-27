// Calibrated PWM control for 3V analog meter with HTTP data fetch
// ESP8266 NodeMCU ESP-12E

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

const char* ssid = "";
const char* password = "";
const char* endpoint = "http://somethin-meter-proxy.noshado.ws/";

const int PWM_PIN = D1;
const int PWM_FREQUENCY = 1000;
const int PWM_RANGE = 1023;
const int CENTER_PWM = 47; // Visual center of meter
const int MAX_PWM = 94; // Visual maximum of meter

unsigned long lastFetch = 0;
const unsigned long fetchInterval = 30000; // 30 seconds

WiFiClient wifiClient;
HTTPClient http;

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(PWM_PIN, OUTPUT);
  analogWriteFreq(PWM_FREQUENCY);
  analogWriteRange(PWM_RANGE);
  
  setMeterValue(0);
  connectToWiFi();
  
  Serial.println("PWM initialized on pin D1");
  Serial.println("Manual commands available:");
  Serial.println("  0.0-1.0 (decimal values, 0.5 = center, 1.0 = max)");
  Serial.println("  'center' (set to 0.5)");
  Serial.println("  'off' (turn off)");
  Serial.println("  'fetch' (manual fetch)");
  Serial.println("");
  Serial.println("Auto-fetching probability every 30 seconds...");
}

void loop() {
  if (millis() - lastFetch >= fetchInterval) {
    fetchProbabilityData();
    lastFetch = millis();
  }
  
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    
    if (command == "center") {
      setMeterValue(0.5);
      Serial.println("Manual: Meter set to CENTER (0.5)");
    } else if (command == "off") {
      analogWrite(PWM_PIN, 0);
      Serial.println("Manual: Meter OFF");
    } else if (command == "fetch") {
      fetchProbabilityData();
    } else {
      float value = command.toFloat();
      if (value >= 0.0 && value <= 1.0) {
        setMeterValue(value);
        Serial.println("Manual: Meter set to " + String(value, 3) + " (" + String(valueToPercentage(value), 1) + "% PWM)");
      } else {
        Serial.println("Invalid input. Use 0.0-1.0, 'center', 'off', or 'fetch'");
        Serial.println("Examples: 0.0 (min), 0.5 (center), 1.0 (max)");
      }
    }
  }
  
  delay(100);
}

void connectToWiFi() {
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println();
  Serial.print("WiFi connected with IP address: ");
  Serial.println(WiFi.localIP());
}

void fetchProbabilityData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, attempting reconnection...");
    connectToWiFi();
    return;
  }
  
  Serial.print("Fetching data from endpoint... ");
  
  http.begin(wifiClient, endpoint);
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  
  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("OK");
      Serial.println("Response: " + payload);
      
      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);
      
      if (!error) {
        float probability = doc["probability"];
        String title = doc["title"];
        
        Serial.println("Title: " + title);
        Serial.println("Probability: " + String(probability, 3));
        
        setMeterValue(probability);
        Serial.println("Meter updated to " + String(probability, 3) + " (" + String(valueToPercentage(probability), 1) + "% PWM)");
      } else {
        Serial.println("JSON parsing failed: " + String(error.c_str()));
      }
    } else {
      Serial.println("HTTP error: " + String(httpCode));
    }
  } else {
    Serial.println("Connection failed: " + String(httpCode));
  }
  
  http.end();
  Serial.println("Next fetch in 30 seconds...");
  Serial.println("");
}

void setMeterValue(float value) {
  value = constrain(value, 0.0, 1.0);
  
  float pwmPercentage;
  
  if (value <= 0.5) {
    pwmPercentage = map(value * 1000, 0, 500, 0, CENTER_PWM * 10) / 10.0;
  } else {
    pwmPercentage = map((value - 0.5) * 1000, 0, 500, CENTER_PWM * 10, MAX_PWM * 10) / 10.0;
  }
  
  int pwmValue = map(pwmPercentage * 10, 0, 1000, 0, PWM_RANGE);
  analogWrite(PWM_PIN, pwmValue);
}

float valueToPercentage(float value) {
  if (value <= 0.5) {
    return map(value * 1000, 0, 500, 0, CENTER_PWM * 10) / 10.0;
  } else {
    return map((value - 0.5) * 1000, 0, 500, CENTER_PWM * 10, MAX_PWM * 10) / 10.0;
  }
}
