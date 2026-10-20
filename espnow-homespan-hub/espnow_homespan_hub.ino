#include <WiFi.h>
#include <esp_now.h>
#include <time.h>
#include <WiFiManager.h>

typedef struct {
  char status[25];
} DoorMessage;

DoorMessage receivedData;

const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -4 * 3600;
const int daylightOffset_sec = 0;

#define RESET_BUTTON_PIN 13
#define RESET_HOLD_DURATION 1000
#define HEARTBEAT_TIMEOUT 1800000

unsigned long buttonPressStart = 0;
bool buttonWasPressed = false;

WiFiManager wifiManager;

const int SENSOR_COUNT = 2;
const char* sensorNames[SENSOR_COUNT] = { "Door sensor 1" };
const char* friendlyNames[SENSOR_COUNT] = { "Sunroom Sensor" };

unsigned long lastHeartbeat[SENSOR_COUNT];
bool isSensorOnline[SENSOR_COUNT];

String formatTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Not Set";
  }

  int month = timeinfo.tm_mon + 1;
  int day = timeinfo.tm_mday;
  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;
  int second = timeinfo.tm_sec;

  bool isPM = hour >= 12;
  int hour12 = hour % 12;
  if (hour12 == 0) hour12 = 12;

  char buffer[30];
  snprintf(buffer, sizeof(buffer), "%d/%d %02d:%02d:%02d %s",
           month, day, hour12, minute, second, isPM ? "PM" : "AM");

  return String(buffer);
}

void OnDataRecv(const esp_now_recv_info_t* recvInfo, const uint8_t* incomingData, int len) {
  memcpy(&receivedData, incomingData, sizeof(receivedData));
  String currentTime = formatTime();
  String originalStatus = String(receivedData.status);
  String displayStatus = originalStatus;

  for (int i = 0; i < SENSOR_COUNT; i++) {
    String baseName = String(sensorNames[i]);
    if (originalStatus.startsWith(baseName)) {
      displayStatus = originalStatus;
      displayStatus.replace(baseName, friendlyNames[i]);
    }

    String expectedStatus = baseName + " online";
    if (originalStatus == expectedStatus) {
      lastHeartbeat[i] = millis();
      if (!isSensorOnline[i]) {
        isSensorOnline[i] = true;
      }
      Serial.print("Heartbeat received from ");
      Serial.println(friendlyNames[i]);
    }
  }

  Serial.print(displayStatus);
  Serial.print(" at ");
  Serial.println(currentTime);
  Serial.println();
}

void checkHeartbeats() {
  unsigned long currentTime = millis();
  for (int i = 0; i < SENSOR_COUNT; i++) {
    if (isSensorOnline[i] && (currentTime - lastHeartbeat[i] > HEARTBEAT_TIMEOUT)) {
      isSensorOnline[i] = false;
      Serial.print(friendlyNames[i]);
      Serial.println(" offline");
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);

  if (!wifiManager.autoConnect("Door Sensor Hub")) {
    Serial.println("Failed to connect. Restarting...");
    ESP.restart();
  }

  Serial.println("Connected to WiFi!");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  Serial.print("Waiting for time synchronization");
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    Serial.print(".");
    delay(500);
  }
  Serial.println("\nTime synchronized!");

  if (esp_now_init() != ESP_OK) {
    Serial.println("Error initializing ESP-NOW");
    return;
  }

  esp_now_register_recv_cb(OnDataRecv);

  for (int i = 0; i < SENSOR_COUNT; i++) {
    lastHeartbeat[i] = millis();
    isSensorOnline[i] = true;
  }
}

void loop() {
  int buttonState = digitalRead(RESET_BUTTON_PIN);

  if (buttonState == LOW && !buttonWasPressed) {
    buttonWasPressed = true;
    buttonPressStart = millis();
  }

  if (buttonState == LOW && buttonWasPressed) {
    if (millis() - buttonPressStart >= RESET_HOLD_DURATION) {
      Serial.println("Reset button held long enough. Clearing WiFi config...");
      wifiManager.resetSettings();
      delay(500);
      ESP.restart();
    }
  }

  if (buttonState == HIGH && buttonWasPressed) {
    buttonWasPressed = false;
  }

  checkHeartbeats();
}