/*
  Scrimmage score board

  4 light beam sensors per goal, 2 goals
  each light beam sensor is mounted in an individual object channel, count objects individually
  4 object channels run in parallel per goal

  Input-Only Pins: Pins like GPIO 34-39 are input-only and do not feature internal pull-up or pull-down resistors, but they still support external interrupts if you provide proper external circuitry.
  Restricted Pins: Avoid using pins tied to flash memory or integrated SPI/boot functions (like GPIO 6–11) for general user interrupts.

*/

#include <Arduino.h>
#include <WiFi.h>
#include <NetworkClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h> // Built-in ESP32 NVS library

Preferences prefs;

enum {
  PREFS_WIFI_MODE_AP,
  PREFS_WIFI_MODE_STATION
};

int wifi_mode = PREFS_WIFI_MODE_AP;
String wifi_ssid = "ScrimmageScoreBoard";
String wifi_pass = "RaileRobotics";

// Define the sensor pin (ensure it supports interrupts)
// - single line of inputs, starting next to Vin, GND:
// Blue side: 4 sensors
#define SENSOR_PIN_B1   13
#define SENSOR_PIN_B2   12
#define SENSOR_PIN_B3   14
#define SENSOR_PIN_B4   27
// Red side: 4 sensors
#define SENSOR_PIN_R1   26
#define SENSOR_PIN_R2   25
#define SENSOR_PIN_R3   33
#define SENSOR_PIN_R4   32

unsigned long trigger_1_sec_time;

// Critical variables must be marked 'volatile' when shared with an ISR
volatile unsigned short int objectCountBlue = 0;
volatile unsigned short int objectCountRed = 0;
//volatile bool beamBroken = false;
unsigned short int lastCountBlue = 0;
unsigned short int lastCountRed = 0;

// Variables to handle hardware switch debouncing
volatile unsigned long currentTime = 0;
volatile unsigned long lastInterruptTimeB1 = 0;
volatile unsigned long lastInterruptTimeB2 = 0;
volatile unsigned long lastInterruptTimeB3 = 0;
volatile unsigned long lastInterruptTimeB4 = 0;
volatile unsigned long lastInterruptTimeR1 = 0;
volatile unsigned long lastInterruptTimeR2 = 0;
volatile unsigned long lastInterruptTimeR3 = 0;
volatile unsigned long lastInterruptTimeR4 = 0;
const unsigned long DEBOUNCE_DELAY = 150; // Milliseconds to ignore false/double triggers
//const unsigned long DEBOUNCE_DELAY = 500; // Milliseconds to ignore false/double triggers

// Define and initialize the spinlock mutex for critical sections
//portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

WebServer server(80);

const int led = 2;  // onboard red LED ?

void handleRoot() {
  digitalWrite(led, 1);
  String message = "Current score: Blue ";
  message += objectCountBlue;
  message += ", Red ";
  message += objectCountRed;
  server.send(200, "text/plain", message);
//  server.send(200, "text/plain", "hello from esp32! What time is it?");
  digitalWrite(led, 0);
}

void handleReset() {
  digitalWrite(led, 1);
  objectCountBlue = 0;
  objectCountRed = 0;
  String message = "Current score: Blue ";
  message += objectCountBlue;
  message += ", Red ";
  message += objectCountRed;
  server.send(200, "text/plain", message);
  digitalWrite(led, 0);
}

void handleNotFound() {
  digitalWrite(led, 1);
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (int i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  digitalWrite(led, 0);
}

// Interrupt Service Routine (ISR) kept as fast and lean as possible
void IRAM_ATTR countObjectISR_B1() {
  currentTime = millis();   // Software debounce checking
  if (currentTime - lastInterruptTimeB1 > DEBOUNCE_DELAY) {
    lastInterruptTimeB1 = currentTime;
    objectCountBlue++;
  }
}

void IRAM_ATTR countObjectISR_B2() {
  currentTime = millis();   // Software debounce checking
  if (currentTime - lastInterruptTimeB2 > DEBOUNCE_DELAY) {
    lastInterruptTimeB2 = currentTime;
    objectCountBlue++;
  }
}

void IRAM_ATTR countObjectISR_B3() {
  if (digitalRead(SENSOR_PIN_B3)) return; // ignore high level, only count falling edge
  currentTime = millis();   // Software debounce checking
  if (currentTime - lastInterruptTimeB3 > DEBOUNCE_DELAY) {
    lastInterruptTimeB3 = currentTime;
    objectCountBlue++;
  }
}

void IRAM_ATTR countObjectISR_B4() {
  if (digitalRead(SENSOR_PIN_B4)) return; // ignore high level, only count falling edge
  currentTime = millis();   // Software debounce checking
  if (currentTime - lastInterruptTimeB4 > DEBOUNCE_DELAY) {
    lastInterruptTimeB4 = currentTime;
    objectCountBlue++;
  }
}

void IRAM_ATTR countObjectISR_R1() {
  if (digitalRead(SENSOR_PIN_R1)) return; // ignore high level, only count falling edge
  currentTime = millis();   // Software debounce checking
  if (currentTime - lastInterruptTimeR1 > DEBOUNCE_DELAY) {
    lastInterruptTimeR1 = currentTime;
    objectCountRed++;
  }
}

void IRAM_ATTR countObjectISR_R2() {
  if (digitalRead(SENSOR_PIN_R2)) return; // ignore high level, only count falling edge
  currentTime = millis();   // Software debounce checking
  if (currentTime - lastInterruptTimeR2 > DEBOUNCE_DELAY) {
    lastInterruptTimeR2 = currentTime;
    objectCountRed++;
  }
}

void IRAM_ATTR countObjectISR_R3() {
  if (digitalRead(SENSOR_PIN_R3)) return; // ignore high level, only count falling edge
  currentTime = millis();   // Software debounce checking
  if (currentTime - lastInterruptTimeR3 > DEBOUNCE_DELAY) {
    lastInterruptTimeR3 = currentTime;
    objectCountRed++;
  }
}

void IRAM_ATTR countObjectISR_R4() {
  if (digitalRead(SENSOR_PIN_R4)) return; // ignore high level, only count falling edge
  currentTime = millis();   // Software debounce checking
  if (currentTime - lastInterruptTimeR4 > DEBOUNCE_DELAY) {
    lastInterruptTimeR4 = currentTime;
    objectCountRed++;
  }
}

//-----------------------------------------------------------------------------
void setup(void) {
  pinMode(led, OUTPUT);
  digitalWrite(led, 0);
  Serial.begin(115200);
  Serial.println("");
  Serial.println("Starting up...");

  trigger_1_sec_time = 0;

  // 1. Open namespace "storage" in Read/Write mode (false = RW, true = RO)
  prefs.begin("storage", false); 

  // 2. Check existence of keys and default them if they are not present
  if (prefs.isKey("wifi_mode"))
    wifi_mode = prefs.getInt("wifi_mode", PREFS_WIFI_MODE_AP);
  else
    prefs.putInt("wifi_mode", wifi_mode);

  if (prefs.isKey("wifi_ssid"))
    prefs.putString("wifi_ssid", wifi_ssid);
  else
    wifi_ssid = prefs.getString("wifi_ssid", "ScrimmageScoreBoard");

  if (prefs.isKey("wifi_pass"))
    prefs.putString("wifi_pass", wifi_pass);
  else
    wifi_pass = prefs.getString("wifi_pass", "RaileRobotics");

  Serial.printf("AP Mode: %d, SSID: %s, Pass: %s\n", wifi_mode, wifi_ssid.c_str(), wifi_pass.c_str());

  // 4. Close the preference to free resources
  prefs.end(); 

  // Wait for connection
  if (wifi_mode == PREFS_WIFI_MODE_AP)
  {
    // Explicitly set the ESP32 to AP Mode
    WiFi.mode(WIFI_AP);

    // Start the Soft Access Point
    // Remove the 'password' parameter if you want an open network
    if (WiFi.softAP(wifi_ssid.c_str(), wifi_pass.c_str())) {
      Serial.println("[+] Access Point started successfully.");
      
      // Get and print the Gateway IP address (Default is usually 192.168.4.1)
      IPAddress apIP = WiFi.softAPIP();
      Serial.print("[+] AP IP Address: ");
      Serial.println(apIP);
    } else {
      Serial.println("[-] Access Point initialization failed.");
    }
  }
  else
  {
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifi_ssid.c_str(), wifi_pass.c_str());
    int retry_count = 0;
    while ((WiFi.status() != WL_CONNECTED) && (retry_count < 10)) {
      delay(500);
      Serial.print(".");
      retry_count++;
    }
    Serial.println("");
    if (retry_count < 10)
    {
      Serial.print("Connected to ");
      Serial.println(wifi_ssid);
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      if (MDNS.begin("ScrimmmageScorer")) {
        Serial.println("MDNS responder started");
        Serial.println(wifi_ssid);
      }
    }
    else
    {
      Serial.print("Timed out attempting to connect to ");
      Serial.println(wifi_ssid);
    }
  }

  server.on("/", handleRoot);
  server.on("/reset", handleReset);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  objectCountBlue = 0;
  objectCountRed = 0;
  lastCountBlue = 0;
  lastCountRed = 0;

  // Configure the sensor pin with an internal pullup resistor 
  // (Optional, but ensures a stable HIGH state when the sensor is unblocked)
  pinMode(SENSOR_PIN_B1, INPUT_PULLUP);
  pinMode(SENSOR_PIN_B2, INPUT_PULLUP);
  pinMode(SENSOR_PIN_B3, INPUT_PULLUP);
  pinMode(SENSOR_PIN_B4, INPUT_PULLUP);
  pinMode(SENSOR_PIN_R1, INPUT_PULLUP);
  pinMode(SENSOR_PIN_R2, INPUT_PULLUP);
  pinMode(SENSOR_PIN_R3, INPUT_PULLUP);
  pinMode(SENSOR_PIN_R4, INPUT_PULLUP);
  
  // Attach interrupt to trigger on a FALLING edge (HIGH to LOW transition when beam breaks)
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN_B1), countObjectISR_B1, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN_B2), countObjectISR_B2, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN_B3), countObjectISR_B3, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN_B4), countObjectISR_B4, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN_R1), countObjectISR_R1, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN_R2), countObjectISR_R2, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN_R3), countObjectISR_R3, FALLING);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN_R4), countObjectISR_R4, FALLING);
  
  Serial.println("System Initialized. Awaiting beam breaks...");
}

void loop(void) {
  server.handleClient();
  delay(2);  //allow the cpu to switch to other tasks

  // Check the flag to handle non-critical UI tasks outside the ISR
  if ((lastCountBlue != objectCountBlue) ||
      (lastCountRed != objectCountRed)) {
    lastCountBlue = objectCountBlue;
    lastCountRed = objectCountRed;
    Serial.print("Current count Blue: ");
    Serial.print(lastCountBlue);
    Serial.print(" Red: ");
    Serial.println(lastCountRed);
  };

/*
  if (beamBroken) {
    // Disable interrupts briefly while reading shared multi-byte data to prevent corruption
    portENTER_CRITICAL_ISR(&mux); 
    int currentCount = objectCount;
    beamBroken = false;
    portEXIT_CRITICAL_ISR(&mux);

    Serial.print("Beam broken! Current count: ");
    Serial.println(currentCount);
  }
*/

/*
  unsigned long current_time = millis();
  if (current_time > trigger_1_sec_time)
  {
    trigger_1_sec_time = current_time + 1000;

    if (digitalRead(SENSOR_PIN_B1))
      Serial.println("B1 high");
    else
      Serial.println("B1 low");
  }
*/
}
