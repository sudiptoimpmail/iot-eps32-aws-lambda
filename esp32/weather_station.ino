// =============================================================================
// Weather IoT — ESP32 sketch
// =============================================================================
// Reads DHT11 every 2s, displays on OLED, sends to AWS Lambda every 30s.
// Device ID is hardcoded as "esp32poc001".
//
// WIRING:
//   DHT11:
//     VCC  -> ESP32 3V3
//     GND  -> ESP32 GND
//     DATA -> ESP32 GPIO 4
//   OLED (0.96" SSD1306, I2C):
//     VCC  -> ESP32 3V3
//     GND  -> ESP32 GND
//     SDA  -> ESP32 GPIO 21
//     SCL  -> ESP32 GPIO 22
//
// LIBRARIES (install via Arduino Library Manager):
//   - DHT sensor library by Adafruit
//   - Adafruit Unified Sensor (auto-suggested)
//   - Adafruit SSD1306
//   - Adafruit GFX Library (auto-suggested)
//   - ArduinoJson by Benoit Blanchon
// =============================================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>


// =============================================================================
// CONFIG  --  EDIT THESE THREE LINES
// =============================================================================
const char* WIFI_SSID     = "YOUR_WIFI_NAME";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
const char* LAMBDA_URL    = "https://YOUR_LAMBDA_URL.lambda-url.us-east-2.on.aws/";


// =============================================================================
// CONSTANTS (no need to change)
// =============================================================================
const char* DEVICE_ID = "esp32poc001";       // hardcoded per requirements

const unsigned long DISPLAY_INTERVAL_MS = 2000;     // refresh OLED every 2s
const unsigned long SEND_INTERVAL_MS    = 30000;    // send to AWS every 30s

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  64
#define OLED_ADDRESS   0x3C
#define DHT_PIN        4
#define DHT_TYPE       DHT11


// =============================================================================
// GLOBALS
// =============================================================================
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
DHT              dht(DHT_PIN, DHT_TYPE);

unsigned long lastDisplayMs = 0;
unsigned long lastSendMs    = 0;
float lastTempF    = NAN;
float lastHumidity = NAN;
int   sendOK   = 0;
int   sendFail = 0;


// =============================================================================
// setup()
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Weather IoT — ESP32 ===");
  Serial.printf("Device ID: %s\n", DEVICE_ID);

  // --- Sensor ---
  dht.begin();

  // --- OLED ---
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("OLED init failed — check wiring");
    while (true) { delay(1000); }
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Booting...");
  display.display();

  // --- WiFi ---
  connectWiFi();
}


// =============================================================================
// connectWiFi()
// =============================================================================
void connectWiFi() {
  Serial.printf("Connecting WiFi: %s\n", WIFI_SSID);

  display.clearDisplay();
  display.setCursor(0, 0);
  display.println("WiFi...");
  display.println(WIFI_SSID);
  display.display();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Connected. IP: %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("WiFi connect failed (will retry in loop)");
  }
}


// =============================================================================
// sendToLambda()
// =============================================================================
bool sendToLambda(float tempF, float humidity) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Skip send: no WiFi");
    return false;
  }

  // Build JSON payload
  StaticJsonDocument<200> doc;
  doc["device_id"] = DEVICE_ID;
  doc["temp_f"]    = tempF;
  doc["humidity"]  = humidity;
  doc["ts"]        = millis();

  String payload;
  serializeJson(doc, payload);
  Serial.printf("→ %s\n", payload.c_str());

  // HTTPS POST
  WiFiClientSecure client;
  client.setInsecure();             // skip cert validation (OK for learning)

  HTTPClient https;
  https.begin(client, LAMBDA_URL);
  https.addHeader("Content-Type", "application/json");
  https.setTimeout(10000);

  int code = https.POST(payload);
  bool ok = false;

  if (code > 0) {
    Serial.printf("← HTTP %d\n", code);
    if (code == 200) {
      String resp = https.getString();
      Serial.printf("← %s\n", resp.c_str());
      ok = true;
    } else {
      String resp = https.getString();
      Serial.printf("← error body: %s\n", resp.c_str());
    }
  } else {
    Serial.printf("← HTTPS error: %s\n", https.errorToString(code).c_str());
  }

  https.end();
  return ok;
}


// =============================================================================
// updateDisplay()
// =============================================================================
void updateDisplay(float tempF, float humidity) {
  display.clearDisplay();

  // Temperature, large
  display.setTextSize(2);
  display.setCursor(0, 0);
  if (isnan(tempF)) {
    display.println("-- F");
  } else {
    display.print(tempF, 1);
    display.println(" F");
  }

  // Humidity
  display.setCursor(0, 22);
  if (isnan(humidity)) {
    display.println("-- %");
  } else {
    display.print(humidity, 0);
    display.println(" %RH");
  }

  // Status line, small
  display.setTextSize(1);
  display.setCursor(0, 48);
  if (WiFi.status() == WL_CONNECTED) {
    display.printf("WiFi OK  AWS:%d/%d", sendOK, sendOK + sendFail);
  } else {
    display.println("WiFi disconnected");
  }

  display.setCursor(0, 56);
  display.printf("Signal: %d dBm", WiFi.RSSI());

  display.display();
}


// =============================================================================
// loop()
// =============================================================================
void loop() {

  // 1. Reconnect WiFi if dropped
  if (WiFi.status() != WL_CONNECTED) {
    static unsigned long lastRetry = 0;
    if (millis() - lastRetry > 30000) {
      lastRetry = millis();
      Serial.println("Reconnecting WiFi...");
      connectWiFi();
    }
  }

  // 2. Read sensor + refresh display every 2 seconds
  if (millis() - lastDisplayMs >= DISPLAY_INTERVAL_MS) {
    lastDisplayMs = millis();

    float tempC    = dht.readTemperature();
    float humidity = dht.readHumidity();

    if (!isnan(tempC) && !isnan(humidity)) {
      lastTempF    = tempC * 9.0 / 5.0 + 32.0;
      lastHumidity = humidity;
    } else {
      Serial.println("DHT read failed (keeping last good values)");
    }

    updateDisplay(lastTempF, lastHumidity);
  }

  // 3. Send to AWS every 30 seconds
  if (millis() - lastSendMs >= SEND_INTERVAL_MS) {
    lastSendMs = millis();
    if (!isnan(lastTempF) && !isnan(lastHumidity)) {
      bool ok = sendToLambda(lastTempF, lastHumidity);
      if (ok) sendOK++;
      else    sendFail++;
    }
  }
}
