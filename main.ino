#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

// --- HARDWARE PINS ---
// RFID 1 (GİRİŞ - Entrance)
#define SS_PIN_1    2
#define RST_PIN_1   22
// RFID 2 (ÇIKIŞ - Exit)
#define SS_PIN_2    4
#define RST_PIN_2   21
// Relay and Buttons
#define RELAY_PIN   13
#define MODE_BUTTON 12
#define DOOR_BUTTON 27 // New button for manual door open

// --- CONFIGURATION ---
// WiFi Credentials
const char* ssid = ""; // Your WiFi SSID
const char* password = ""; // Your WiFi Password

// Google Script and Sheet URLs
const char* googleScriptURL = ""; // Your Google Apps Script URL
const char* googleUIDSheetCSV = ""; // Direct CSV link to your UID list sheet

// Statically defined ADMIN card UIDs
const byte ADMIN_CARDS[][4] = {
  {0x7B, 0x69, 0xF8, 0x11},
  {0x7B, 0x16, 0x01, 0x11}
  // Add more admin card UIDs here if needed
};
const int ADMIN_COUNT = sizeof(ADMIN_CARDS) / sizeof(ADMIN_CARDS[0]);

// --- SYSTEM SETTINGS ---
#define MAX_UIDS 100 // Maximum number of UIDs to store in local cache
const unsigned long RELAY_OPEN_DURATION_MS = 5000; // How long the relay stays active (5 seconds)
const unsigned long RFID_COOLDOWN_MS = 2000;       // Prevent re-reading the same card for 2 seconds
const unsigned long BUTTON_DEBOUNCE_MS = 50;       // Debounce delay for the mode button

// --- GLOBAL VARIABLES & OBJECTS ---
MFRC522 rfid1(SS_PIN_1, RST_PIN_1);
MFRC522 rfid2(SS_PIN_2, RST_PIN_2);

// System Modes
enum Mode { DOOR_UNLOCK, UID_LEARN };
Mode currentMode = DOOR_UNLOCK;

// Local cache for authorized UIDs fetched from Google Sheets
String learnedUIDs[MAX_UIDS];
int learnedCount = 0;

// Non-blocking timer variables
unsigned long relayActivationTime = 0;
unsigned long rfid1_lastReadTime = 0;
unsigned long rfid2_lastReadTime = 0;

// FreeRTOS handles for task communication
QueueHandle_t httpQueue;
TaskHandle_t httpTaskHandle;

// --- FUNCTION PROTOTYPES ---
String getUIDString(byte *buffer, byte bufferSize);
bool compareUID(byte *u1, const byte *u2);
String getFormattedTime();

// --- HTTP POST TASK (RUNS ON CORE 0) ---
void httpPostTask(void *pvParameters) {
  Serial.println("HTTP Post Task started on Core 0.");
  char jsonPayload[256];

  for (;;) {
    if (xQueueReceive(httpQueue, &jsonPayload, portMAX_DELAY) == pdPASS) {
      if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin(googleScriptURL);
        http.addHeader("Content-Type", "application/json");
        http.setTimeout(15000); 

        Serial.printf("[HTTP Task] Sending POST: %s\n", jsonPayload);
        int httpCode = http.POST(String(jsonPayload));

        if (httpCode > 0) {
          String response = http.getString();
          Serial.printf("[HTTP Task] Response Code: %d, Response: %s\n", httpCode, response.c_str());
        } else {
          Serial.printf("[HTTP Task] POST failed, error: %s\n", http.errorToString(httpCode).c_str());
        }
        http.end();
      } else {
        Serial.println("[HTTP Task] WiFi not connected. Skipping POST.");
      }
    }
  }
}

// --- SETUP ---
void setup() {
  pinMode(MODE_BUTTON, INPUT_PULLUP);
  pinMode(DOOR_BUTTON, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); 
  Serial.begin(115200);
  Serial.println("\n--- RFID Access Control System Initializing ---");

  httpQueue = xQueueCreate(10, sizeof(char) * 256);
  xTaskCreatePinnedToCore(httpPostTask, "HTTP Post Task", 10000, NULL, 1, &httpTaskHandle, 0);

  SPI.begin(18, 19, 23);
  rfid1.PCD_Init();
  rfid2.PCD_Init();
  Serial.println("RFID Readers Initialized.");

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected! IP Address: " + WiFi.localIP().toString());

  configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
  Serial.print("Waiting for NTP time sync");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nNTP Time Synchronized.");

  fetchUIDsFromSheet();
}

// --- CORE LOGIC & HANDLERS ---

// *** THIS FUNCTION HAS BEEN UPDATED ***
// Fetches UIDs from the two-column CSV and parses correctly.
void fetchUIDsFromSheet() {
  learnedCount = 0;
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Cannot fetch UIDs, WiFi not connected.");
    return;
  }
  
  HTTPClient http;
  Serial.println("Fetching UIDs from Google Sheet...");
  http.begin(googleUIDSheetCSV);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  int httpCode = http.GET();

  if (httpCode == 200) {
    String csv = http.getString();
    int idx = 0;
    while (idx < csv.length() && learnedCount < MAX_UIDS) {
      int nl = csv.indexOf('\n', idx);
      String line = (nl > idx ? csv.substring(idx, nl) : csv.substring(idx));
      line.trim();
      idx = (nl < 0 ? csv.length() : nl + 1);

      if (line.length() > 0) {
        String uid;
        int commaIndex = line.indexOf(',');
        
        if (commaIndex != -1) {
          // Comma found, extract the part before it as the UID
          uid = line.substring(0, commaIndex);
        } else {
          // No comma, assume the whole line is the UID (for backward compatibility)
          uid = line;
        }
        uid.trim(); // Clean up any extra spaces

        // Add to the array if it's a valid-looking UID (and not the header)
        if (uid.length() > 1 && !uid.equalsIgnoreCase("Card UID")) {
            learnedUIDs[learnedCount++] = uid;
        }
      }
    }
    Serial.printf("Successfully fetched and parsed %d learned UIDs.\n", learnedCount);
  } else {
    Serial.println("Failed to fetch UID list. HTTP Code: " + String(httpCode));
  }
  http.end();
}

bool isLearnedUID(const String &uid) {
  for (int i = 0; i < learnedCount; i++) {
    if (uid.equalsIgnoreCase(learnedUIDs[i])) return true;
  }
  return false;
}

void activateRelay() {
  Serial.println("Access granted. Activating relay.");
  digitalWrite(RELAY_PIN, LOW);
  relayActivationTime = millis();
}

void handleRelay() {
  if (relayActivationTime > 0 && (millis() - relayActivationTime >= RELAY_OPEN_DURATION_MS)) {
    digitalWrite(RELAY_PIN, HIGH);
    relayActivationTime = 0;
    Serial.println("Relay deactivated.");
  }
}

// Non-blocking function to check for button presses
void handleButtons() {
  static unsigned long lastCheckTime = 0;
  static bool lastModeButtonState = HIGH;
  static bool lastDoorButtonState = HIGH;

  if (millis() - lastCheckTime > BUTTON_DEBOUNCE_MS) {
    // Check Mode Button
    bool currentModeButtonState = digitalRead(MODE_BUTTON);
    if (lastModeButtonState == HIGH && currentModeButtonState == LOW) {
      currentMode = (currentMode == DOOR_UNLOCK ? UID_LEARN : DOOR_UNLOCK);
      Serial.println("\n*************************************************");
      Serial.print("** MODE SWITCHED TO: ");
      Serial.println(currentMode == DOOR_UNLOCK ? "DOOR_UNLOCK **" : "UID_LEARN **");
      Serial.println("*************************************************");
      if (currentMode == DOOR_UNLOCK) {
        fetchUIDsFromSheet();
      }
    }
    lastModeButtonState = currentModeButtonState;

    // Check Manual Door Open Button
    bool currentDoorButtonState = digitalRead(DOOR_BUTTON);
    if (lastDoorButtonState == HIGH && currentDoorButtonState == LOW) {
      Serial.println("Manual door open button pressed.");
      activateRelay();
      
      // Send a log entry for the manual open event
      String timestamp = getFormattedTime();
      char jsonPayload[256];
      snprintf(jsonPayload, sizeof(jsonPayload),
               "{\"operation\":\"LOG\",\"location\":\"BUTTON\",\"uid\":\"N/A\",\"timestamp\":\"%s\",\"role\":\"MANUAL_OPEN\"}",
               timestamp.c_str());
      xQueueSend(httpQueue, &jsonPayload, portMAX_DELAY);
    }
    lastDoorButtonState = currentDoorButtonState;

    lastCheckTime = millis();
  }
}

void processUIDLearn(MFRC522 &reader) {
  if (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) {
    return;
  }

  for (int i = 0; i < ADMIN_COUNT; i++) {
    if (compareUID(reader.uid.uidByte, ADMIN_CARDS[i])) {
      Serial.println("Admin card presented in learn mode. Ignoring.");
      reader.PICC_HaltA();
      return;
    }
  }

  String uid = getUIDString(reader.uid.uidByte, reader.uid.size);
  Serial.println("Learn Mode: Scanned UID: " + uid);

  char jsonPayload[256];
  snprintf(jsonPayload, sizeof(jsonPayload), "{\"operation\":\"LEARN\",\"uid\":\"%s\"}", uid.c_str());
  xQueueSend(httpQueue, &jsonPayload, portMAX_DELAY);

  if (!isLearnedUID(uid) && learnedCount < MAX_UIDS) {
    learnedUIDs[learnedCount++] = uid;
    Serial.println("UID added to local cache.");
  }

  reader.PICC_HaltA();
}

void processDoor(MFRC522 &reader, const char* location) {
  if (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) {
    return;
  }

  String uid = getUIDString(reader.uid.uidByte, reader.uid.size);
  String role = "NONE";
  bool authorized = false;

  for (int i = 0; i < ADMIN_COUNT; i++) {
    if (compareUID(reader.uid.uidByte, ADMIN_CARDS[i])) {
      role = "ADMIN";
      authorized = true;
      break;
    }
  }

  if (!authorized && isLearnedUID(uid)) {
    role = "GUEST";
    authorized = true;
  }

  Serial.printf("Location: %s | Scanned UID: %s | Role: %s | Access: %s\n",
                location, uid.c_str(), role.c_str(), authorized ? "GRANTED" : "DENIED");

  String timestamp = getFormattedTime();
  char jsonPayload[256];
  snprintf(jsonPayload, sizeof(jsonPayload),
           "{\"operation\":\"LOG\",\"location\":\"%s\",\"uid\":\"%s\",\"timestamp\":\"%s\",\"role\":\"%s\"}",
           location, uid.c_str(), timestamp.c_str(), role.c_str());
  xQueueSend(httpQueue, &jsonPayload, portMAX_DELAY);

  if (authorized) {
    activateRelay();
  }

  reader.PICC_HaltA();
}

// --- MAIN LOOP (RUNS ON CORE 1) ---
void loop() {
  handleButtons(); // Combined handler for both buttons
  handleRelay();

  unsigned long currentTime = millis();

  if (currentMode == UID_LEARN) {
    if (currentTime - rfid1_lastReadTime > RFID_COOLDOWN_MS) {
      processUIDLearn(rfid1);
      rfid1_lastReadTime = currentTime;
    }
    if (currentTime - rfid2_lastReadTime > RFID_COOLDOWN_MS) {
      processUIDLearn(rfid2);
      rfid2_lastReadTime = currentTime;
    }
  } else { // DOOR_UNLOCK mode
    if (currentTime - rfid1_lastReadTime > RFID_COOLDOWN_MS) {
      processDoor(rfid1, "GIRIS");
      rfid1_lastReadTime = currentTime;
    }
    if (currentTime - rfid2_lastReadTime > RFID_COOLDOWN_MS) {
      processDoor(rfid2, "CIKIS");
      rfid2_lastReadTime = currentTime;
    }
  }
}

// --- UTILITY FUNCTIONS ---
String getUIDString(byte *buffer, byte bufferSize) {
  String uid = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) {
      uid += "0";
    }
    uid += String(buffer[i], HEX);
    if (i < bufferSize - 1) {
      uid += ":";
    }
  }
  uid.toUpperCase();
  return uid;
}

bool compareUID(byte *u1, const byte *u2) {
  for (byte i = 0; i < 4; i++) {
    if (u1[i] != u2[i]) return false;
  }
  return true;
}

String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time not synced";
  }
  char buf[20];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
  return String(buf);
}
