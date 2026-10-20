#include <esp_now.h>
#include <esp_sleep.h>
#include <WiFi.h>

#define SWITCH_PIN 15
#define HEARTBEAT_INTERVAL_MINUTES 10

uint8_t peerAddress[] = {0x38, 0x18, 0x2B, 0x8B, 0xFC, 0x88};

typedef struct {
  char status[25];
} DoorMessage;

RTC_DATA_ATTR int lastState = -1;
RTC_DATA_ATTR unsigned long lastHeartbeat = 0;

DoorMessage message;

bool wifiActive = false;
bool sendSwitchUpdate = false;
bool sendHeartbeat = false;
unsigned long maxDelayMs = 0;

void sendESPNowMessage(const char* status) {
  strcpy(message.status, status);
  esp_now_send(peerAddress, (uint8_t*)&message, sizeof(message));
  Serial.print("Sent: ");
  Serial.println(message.status);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  pinMode(SWITCH_PIN, INPUT_PULLUP);
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  if (!esp_now_is_peer_exist(peerAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("Failed to add peer");
    }
  }

  esp_sleep_wakeup_cause_t reason = esp_sleep_get_wakeup_cause();
  int currentState = digitalRead(SWITCH_PIN);

  if (reason == ESP_SLEEP_WAKEUP_EXT0) {
    if (currentState != lastState) {
      lastState = currentState;
      sendSwitchUpdate = true;
      maxDelayMs = 5000;
    }
  } else if (reason == ESP_SLEEP_WAKEUP_TIMER) {
    sendHeartbeat = true;
    maxDelayMs = 5000;
  } else {
    if (currentState != lastState) {
      lastState = currentState;
      sendSwitchUpdate = true;
      maxDelayMs = 5000;
    }
    sendHeartbeat = true;
  }

  if (sendSwitchUpdate) {
    if (currentState == LOW) {
      sendESPNowMessage("Door sensor 1 open");
    } else {
      sendESPNowMessage("Door sensor 1 closed");
    }
  }

  if (sendHeartbeat) {
    sendESPNowMessage("Door sensor 1 online");
    lastHeartbeat = millis();
  }

  if (maxDelayMs > 0) {
    unsigned long activeStart = millis();
    while (millis() - activeStart < maxDelayMs) {
      int nowState = digitalRead(SWITCH_PIN);
      if (nowState != lastState) {
        lastState = nowState;
        activeStart = millis();

        if (nowState == LOW) {
          sendESPNowMessage("Door sensor 1 open");
        } else {
          sendESPNowMessage("Door sensor 1 closed");
        }
      }
      delay(50);
    }
  }

  Serial.println("Preparing to sleep...");
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_15, lastState == LOW ? HIGH : LOW);
  esp_sleep_enable_timer_wakeup(HEARTBEAT_INTERVAL_MINUTES * 60ULL * 1000000ULL);
  Serial.println("Going to sleep...");
  esp_deep_sleep_start();
}

void loop() {

}