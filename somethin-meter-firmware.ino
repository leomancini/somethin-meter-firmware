// Calibrated PWM control for 3V analog meter with HTTP data fetch and LCD display
// ESP8266 NodeMCU ESP-12E

#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>

const char* ssid = "";
const char* password = "";
const char* endpoint = "http://somethin-meter-proxy.noshado.ws/";

// Pin assignments
const int PWM_PIN = D3;  // Changed from D1 (D1 is now SCL for I2C)
const int LED_PIN = D4;
const int SDA_PIN = D2;  // GPIO4
const int SCL_PIN = D1;  // GPIO5

// PWM settings
const int PWM_FREQUENCY = 1000;
const int PWM_RANGE = 1023;
const int CENTER_PWM = 46;  // Visual center of meter
const int MAX_PWM = 91;     // Visual maximum of meter

// Timing
unsigned long lastFetch = 0;
const unsigned long fetchInterval = 30000;  // 30 seconds

// Network and HTTP
WiFiClient wifiClient;
HTTPClient http;

// LCD
hd44780_I2Cexp lcd;

// Data storage
String currentTitle = "";
float currentProbability = 0.0;
int currentVolume = 0;

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Initialize pins
  pinMode(PWM_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  analogWriteFreq(PWM_FREQUENCY);
  analogWriteRange(PWM_RANGE);

  // Initialize I2C with specific pins
  Wire.begin(SDA_PIN, SCL_PIN);

  // Initialize LCD
  Serial.println("Initializing LCD...");
  int status = lcd.begin(20, 4);
  if (status) {
    Serial.print("20x4 LCD failed, trying 16x2. Status: ");
    Serial.println(status);
    status = lcd.begin(16, 2);
  }

  if (status == 0) {
    lcd.backlight();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Starting up...");
    Serial.println("LCD initialized successfully");
  } else {
    Serial.print("LCD initialization failed completely. Status: ");
    Serial.println(status);
  }

  // Keep external LED off until WiFi connects
  analogWrite(LED_PIN, 0);

  setMeterValue(0);
  connectToWiFi();

  Serial.println("PWM initialized on pin D3");
  Serial.println("Status LED on pin D4");
  Serial.println("LCD initialized on I2C (SDA=D2, SCL=D1)");
  Serial.println("Manual commands available:");
  Serial.println("  0.0-1.0 (decimal values, 0.5 = center, 1.0 = max)");
  Serial.println("  'center' (set to 0.5)");
  Serial.println("  'off' (turn off)");
  Serial.println("  'fetch' (manual fetch)");
  Serial.println("");
  Serial.println("Auto-fetching probability every 30 seconds...");

  // Fetch immediately on startup
  fetchProbabilityData();
  lastFetch = millis();
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
      updateLCDManual(0.5);
      Serial.println("Manual: Meter set to CENTER (0.5)");
    } else if (command == "off") {
      analogWrite(PWM_PIN, 0);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("METER OFF");
      Serial.println("Manual: Meter OFF");
    } else if (command == "fetch") {
      fetchProbabilityData();
    } else {
      float value = command.toFloat();
      if (value >= 0.0 && value <= 1.0) {
        setMeterValue(value);
        updateLCDManual(value);
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

  // Keep external LED off while connecting
  analogWrite(LED_PIN, 0);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    analogWrite(LED_PIN, 0);
  }

  Serial.println();
  Serial.print("WiFi connected with IP address: ");
  Serial.println(WiFi.localIP());

  // Turn on external LED only after WiFi is connected
  analogWrite(LED_PIN, 25);
  Serial.println("Status LED: ON (WiFi connected)");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi Connected");
  lcd.setCursor(0, 1);
  lcd.print("IP: ");
  lcd.print(WiFi.localIP());
  delay(2000);
}

void blinkLED() {
  // Quick blink to indicate data received
  analogWrite(LED_PIN, 0);
  delay(100);
  analogWrite(LED_PIN, 25);
  delay(100);
  analogWrite(LED_PIN, 0);
  delay(100);
  analogWrite(LED_PIN, 25);
}

void fetchProbabilityData() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, attempting reconnection...");
    analogWrite(LED_PIN, 0);  // Turn off external LED when disconnected
    connectToWiFi();
    return;
  }

  Serial.print("Fetching data from endpoint... ");

  // Show fetching status on LCD
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Fetching data...");

  http.begin(wifiClient, endpoint);
  http.setTimeout(10000);

  int httpCode = http.GET();

  if (httpCode > 0) {
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      Serial.println("OK");
      Serial.println("Response: " + payload);

      // Blink LED to indicate successful data fetch
      blinkLED();

      DynamicJsonDocument doc(1024);
      DeserializationError error = deserializeJson(doc, payload);

      if (!error) {
        currentProbability = doc["probability"];
        currentTitle = doc["title"].as<String>();
        currentVolume = doc["volume"];

        Serial.println("Title: " + currentTitle);
        Serial.println("Probability: " + String(currentProbability, 3));
        Serial.println("Volume: " + String(currentVolume));

        setMeterValue(currentProbability);
        updateLCDWithData();

        Serial.println("Meter updated to " + String(currentProbability, 3) + " (" + String(valueToPercentage(currentProbability), 1) + "% PWM)");
      } else {
        Serial.println("JSON parsing failed: " + String(error.c_str()));
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print("JSON Parse Error");
        lcd.setCursor(0, 1);
        lcd.print(error.c_str());
      }
    } else {
      Serial.println("HTTP error: " + String(httpCode));
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("HTTP Error");
      lcd.setCursor(0, 1);
      lcd.print("Code: " + String(httpCode));
    }
  } else {
    Serial.println("Connection failed: " + String(httpCode));
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connection Failed");
    lcd.setCursor(0, 1);
    lcd.print("Error: " + String(httpCode));
  }

  http.end();
  Serial.println("Next fetch in 30 seconds...");
  Serial.println("");
}

void updateLCDWithData() {
  lcd.clear();

  // Display the full title across multiple lines if needed
  displayWrappedText(currentTitle, 0);

  // // Display probability and volume on the last line
  // lcd.setCursor(0, 3);
  // lcd.print("Prob: ");
  // lcd.print(String(currentProbability * 100, 1));
  // lcd.print("%");

  // // Add volume with formatting
  // String volumeStr = " Vol: $" + formatVolume(currentVolume);
  // int probText = 6 + String(currentProbability * 100, 1).length() + 1; // "Prob: XX.X%"
  // int remainingSpace = 20 - probText;
  // if (volumeStr.length() <= remainingSpace) {
  //   lcd.print(volumeStr);
  // }
}

void displayWrappedText(String text, int startRow) {
  int maxRows = 4;
  int maxCols = 20;
  int currentRow = startRow;
  int currentCol = 0;

  // Split text into words
  int wordStart = 0;

  for (int i = 0; i <= text.length(); i++) {
    if (i == text.length() || text.charAt(i) == ' ') {
      // Found end of word or end of string
      String word = text.substring(wordStart, i);

      // Check if word fits on current line
      if (currentCol + word.length() > maxCols) {
        // Move to next row
        currentRow++;
        currentCol = 0;

        // Check if we have room for more rows
        if (currentRow >= startRow + maxRows) {
          break;  // No more room
        }
      }

      // Display the word
      lcd.setCursor(currentCol, currentRow);
      lcd.print(word);
      currentCol += word.length();

      // Add space after word (if not at end of text and room available)
      if (i < text.length() && currentCol < maxCols) {
        lcd.print(" ");
        currentCol++;
      }

      // Set start of next word
      wordStart = i + 1;
    }
  }
}

void updateLCDManual(float value) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Manual Mode");
  lcd.setCursor(0, 1);
  lcd.print("Test Value Set");
  lcd.setCursor(0, 2);
  lcd.print("Probability: ");
  lcd.print(String(value * 100, 1));
  lcd.print("%");
}

String formatVolume(int volume) {
  if (volume >= 1000000) {
    return String(volume / 1000000) + "M";
  } else if (volume >= 1000) {
    return String(volume / 1000) + "K";
  } else {
    return String(volume);
  }
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
