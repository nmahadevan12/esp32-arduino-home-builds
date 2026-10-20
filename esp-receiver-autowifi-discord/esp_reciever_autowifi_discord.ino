#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <time.h>
#include <FS.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"

#define HOME_FILE "/home.txt"
#define CRED_USER_FILE "/user.txt"
#define CRED_PASS_FILE "/pass.txt"
#define WEBHOOK_FILE "/webhook.txt"
#define MAX_MESSAGES 80
#define HEARTBEAT_TIMEOUT_MINUTES 30
#define MAX_SENSORS 10

bool isHome = false;
bool warned70 = false;
bool warned90 = false;
bool lockoutMessageSent = false;
bool loadHomeStatus() {
  if (!SPIFFS.exists(HOME_FILE)) {
    Serial.println("No saved home status found. Defaulting to away.");
    return false;
  }

  File file = SPIFFS.open(HOME_FILE, FILE_READ);
  if (!file) {
    Serial.println("Failed to open home file for reading");
    return false;
  }

  char status = file.read();
  file.close();
  return (status == '1');
}
bool isValidTimestamp(const String& ts) {
  if (ts.length() != 19) return false;

  if (ts.charAt(2) != '/' || ts.charAt(5) != '/') return false;

  if (ts.charAt(10) != ' ') return false;

  if (ts.charAt(13) != ':' || ts.charAt(16) != ':') return false;

  int month = ts.substring(0, 2).toInt();
  int day = ts.substring(3, 5).toInt();
  int year = ts.substring(6, 10).toInt();

  int hour = ts.substring(11, 13).toInt();
  int minute = ts.substring(14, 16).toInt();
  int second = ts.substring(17, 19).toInt();

  if (month < 1 || month > 12) return false;
  if (day < 1 || day > 31) return false;
  if (year < 2000 || year > 2100) return false;

  if (hour < 0 || hour > 23) return false;
  if (minute < 0 || minute > 59) return false;
  if (second < 0 || second > 59) return false;

  return true;
}

const uint16_t sensorPort = 1234;
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -4 * 3600;
const int daylightOffset_sec = 0;
const int resetPin = 13;
const unsigned long sessionTimeout = 30UL * 60UL * 1000UL;

unsigned long resetButtonPressedTime = 0;
unsigned long sessionStart = 0;
unsigned long lockoutEndTime = 0;
unsigned int failedAttempts = 0;

struct SensorStatus {
  unsigned long lastHeartbeatTime = 0;
  bool isOnline = false;
};

String wifi_ssid;
String wifi_pass;
String api_token = "";
String webhook_url = "";
String stored_username_hash = "";
String stored_password_hash = "";
String messages[MAX_MESSAGES];
String loadFromFile(const char* path) {
  File file = SPIFFS.open(path, "r");
  if (!file) return "";
  String data = file.readString();
  file.close();
  return data;
}
String sha256Hash(const String& input) {
  byte hash[32];
  mbedtls_sha256_context ctx;

  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, (const unsigned char*)input.c_str(), input.length());
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);

  char hashString[65];
  for (int i = 0; i < 32; ++i) {
    sprintf(&hashString[i * 2], "%02x", hash[i]);
  }

  return String(hashString);
}
String base64Decode(const String& input) {
  size_t outputLen = 128;
  uint8_t output[128] = { 0 };

  int ret = mbedtls_base64_decode(output, sizeof(output), &outputLen,
                                  (const unsigned char*)input.c_str(), input.length());
  if (ret == 0) {
    return String((char*)output).substring(0, outputLen);
  } else {
    Serial.printf("Base64 decode failed: -0x%04x\n", -ret);
    return String("");
  }
}
String formatTimestamp(const String& ts) {
  if (!isValidTimestamp(ts)) return "Invalid Timestamp";

  int month = ts.substring(0, 2).toInt();
  int day = ts.substring(3, 5).toInt();
  int hour = ts.substring(11, 13).toInt();
  String minute = ts.substring(14, 16);
  String second = ts.substring(17, 19);

  String period = "AM";
  if (hour >= 12) {
    period = "PM";
    if (hour > 12) hour -= 12;
  } else if (hour == 0) {
    hour = 12;
  }

  return String(month) + "/" + String(day) + " " + String(hour) + ":" + minute + ":" + second + " " + period;
}
String loadApiToken() {
  File file = SPIFFS.open("/api_token.txt", "r");
  if (file) {
    String token = file.readString();
    token.trim();
    file.close();
    return token;
  } else {
    return "";
  }
}
String loadWebhookFromFile() {
  if (!SPIFFS.exists(WEBHOOK_FILE)) {
    Serial.println("No saved webhook found.");
    return "";
  }

  File file = SPIFFS.open(WEBHOOK_FILE, FILE_READ);
  if (!file) {
    Serial.println("Failed to open webhook file for reading");
    return "";
  }

  String url = file.readStringUntil('\0');
  file.close();
  Serial.println("Webhook loaded from SPIFFS: " + url);
  return url;
}
String urlencode(String str) {
  String encoded = "";
  char c;
  char code0, code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      encoded += '%';
      code0 = (c >> 4) & 0xF;
      code1 = c & 0xF;
      encoded += char(code0 > 9 ? code0 - 10 + 'A' : code0 + '0');
      encoded += char(code1 > 9 ? code1 - 10 + 'A' : code1 + '0');
    }
  }
  return encoded;
}
String sensorNames[MAX_SENSORS] = { "Sunroom", "Garage" };

SensorStatus sensorStatuses[MAX_SENSORS];

int messageCount = 0;
int downloadCounter = 1;
int findSensorIndex(const String& sensorName) {
  for (int i = 0; i < MAX_SENSORS; i++) {
    if (sensorName.equalsIgnoreCase(sensorNames[i])) return i;
  }
  return -1;
}

WiFiServer tcpServer(1234);
WebServer webServer(80);
WiFiManager wm;

void setup() {
  pinMode(resetPin, INPUT_PULLUP);

  Serial.begin(115200);

  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
  } else {
    String loaded = loadWebhookFromFile();
    if (loaded.length() > 0) {
      webhook_url = loaded;
    }

    stored_username_hash = loadFromFile(CRED_USER_FILE);
    stored_password_hash = loadFromFile(CRED_PASS_FILE);

    api_token = loadApiToken();
    Serial.println("Loaded API token: " + api_token);

    if (stored_username_hash.length() == 0 || stored_password_hash.length() == 0) {
      Serial.println("No stored credentials found, setting default username and password.");

      stored_username_hash = sha256Hash("doorsensor");
      stored_password_hash = sha256Hash("esp32");

      saveToFile(CRED_USER_FILE, stored_username_hash);
      saveToFile(CRED_PASS_FILE, stored_password_hash);
    }
  }

  isHome = loadHomeStatus();

  Serial.println("Starting WiFiManager to connect...");
  bool res = wm.autoConnect("DoorSensorHub", "password");

  if (!res) {
    Serial.println("Failed to connect using WiFiManager, restarting...");
    ESP.restart();
  }

  Serial.println("WiFi connected: " + WiFi.localIP().toString());

  if (!res) {
    Serial.println("Failed to connect");
    ESP.restart();
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Waiting for time synchronization...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime synchronized");

  tcpServer.begin();
  Serial.println("TCP server started on port 1234");

  webServer.on("/update_webhook", HTTP_POST, []() {
    if (webServer.hasArg("webhook")) {
      webhook_url = webServer.arg("webhook");
      saveWebhookToFile(webhook_url);
      Serial.println("Webhook updated to: " + webhook_url);
    }
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  });

  webServer.on("/", HTTP_GET, []() {
    unsigned long now = millis();

    if (webServer.hasHeader("Authorization")) {
      String auth = webServer.header("Authorization");
      auth.replace("Basic ", "");
      String decoded = base64Decode(auth);

      int sep = decoded.indexOf(":");
      if (sep != -1) {
        String user = decoded.substring(0, sep);
        String pass = decoded.substring(sep + 1);

        if (sha256Hash(user) == String(stored_username_hash) && sha256Hash(pass) == String(stored_password_hash)) {

          failedAttempts = 0;
          lockoutEndTime = 0;
          lockoutMessageSent = false;
          sessionStart = now;

          webServer.sendHeader("Set-Cookie", "ESPSESSION=true; Max-Age=1800");
          handleRoot();
          return;
        }
      }
    }

    if (now < lockoutEndTime) {
      String page = R"rawliteral(
    <html lang='en'>
    <head>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <title>Locked Out</title>
    <link href='https://fonts.googleapis.com/css2?family=Roboto:wght@300;400;700&display=swap' rel='stylesheet'>
    <style>
      body {
        font-family: 'Roboto', sans-serif;
        margin: 0;
        padding: 0;
        background-color: #eef2f7;
        color: #34495e;
        line-height: 1.6;
        display: flex;
        justify-content: center;
        align-items: center;
        min-height: 100vh;
        text-align: center;
      }

      .container {
        max-width: 500px;
        margin: 20px auto;
        padding: 25px;
        background-color: #ffffff;
        border-radius: 12px;
        box-shadow: 0 6px 20px rgba(0, 0, 0, 0.08);
      }

      h1 {
        font-size: 2em;
        color: #dc3545;
        margin-bottom: 20px;
        font-weight: 700;
      }

      p {
        font-size: 1.1em;
        color: #555;
      }

      #countdown {
        font-weight: bold;
        color: #007bff;
      }

      @media (max-width: 600px) {
        body {
          padding: 15px;
        }
        .container {
          margin: 10px;
          padding: 20px;
          border-radius: 8px;
        }
        h1 {
          font-size: 1.8em;
        }
        p {
          font-size: 1em;
        }
      }
    </style>
    <script>
      function updateCountdown() {
        fetch('/lockout_time')
          .then(response => {
            if (!response.ok) {
              throw new Error('Network response was not ok ' + response.statusText);
            }
            return response.text();
          })
          .then(seconds => {
            const sec = parseInt(seconds);
            if (sec > 0) {
              document.getElementById('countdown').textContent = sec;
              setTimeout(updateCountdown, 1000);
            } else {
              location.reload();
            }
          })
          .catch(err => {
            console.error('Error fetching lockout time:', err);
            const countdownElement = document.getElementById('countdown');
            if (countdownElement) {
              countdownElement.textContent = '...';
            }
            setTimeout(updateCountdown, 3000);
          });
      }
      window.onload = updateCountdown;
    </script>
    </head>
    <body>
      <div class='container'>
        <h1>Too many failed login attempts.</h1>
        <p>Please wait <span id="countdown"></span> seconds before trying again.</p>
      </div>
    </body>
    </html>
      )rawliteral";

      webServer.send(403, "text/html", page);
      return;
    }

    if (webServer.hasHeader("Cookie")) {
      String cookie = webServer.header("Cookie");
      if (cookie.indexOf("ESPSESSION=true") != -1) {
        if (now - sessionStart > sessionTimeout) {
          sessionStart = 0;
          failedAttempts = 0;
          return webServer.requestAuthentication();
        } else {
          sessionStart = now;
          handleRoot();
          return;
        }
      }
    }

    failedAttempts++;
    if (failedAttempts >= 5) {
      unsigned long lockoutSeconds = 30UL << (failedAttempts - 5);
      lockoutEndTime = now + lockoutSeconds * 1000UL;

      if (!lockoutMessageSent) {
        String message = "⚠️ Lockout triggered due to failed login attempts. ";
        message += "Lockout duration: " + String(lockoutSeconds) + " seconds.";
        sendDiscordMessage(message);
        lockoutMessageSent = true;
      }

      Serial.printf("Locking out for %lu seconds after %u failed attempts\n", lockoutSeconds, failedAttempts);
    }

    return webServer.requestAuthentication();
  });

  webServer.on("/lockout_time", HTTP_GET, []() {
    unsigned long now = millis();
    if (now >= lockoutEndTime) {
      webServer.send(200, "text/plain", "0");
    } else {
      unsigned long waitMs = lockoutEndTime - now;
      int waitSec = (waitMs + 999) / 1000;
      webServer.send(200, "text/plain", String(waitSec));
    }
  });

  webServer.on("/download", HTTP_GET, handleDownload);
  webServer.on("/clear", HTTP_GET, handleClearForm);
  webServer.on("/confirm_clear", HTTP_POST, handleClearLogs);

  webServer.on("/toggle_home", HTTP_POST, []() {
    isHome = !isHome;
    saveHomeStatus(isHome);
    webServer.sendHeader("Location", "/");
    webServer.send(303);
  });

  webServer.on("/update_credentials", HTTP_POST, []() {
    if (webServer.hasArg("username") && webServer.hasArg("password")) {
      String user = webServer.arg("username");
      String pass = webServer.arg("password");

      stored_username_hash = sha256Hash(user);
      stored_password_hash = sha256Hash(pass);

      saveToFile(CRED_USER_FILE, stored_username_hash);
      saveToFile(CRED_PASS_FILE, stored_password_hash);

      String page = R"rawliteral(
        <!DOCTYPE html>
        <html lang="en">
        <head>
          <meta charset="UTF-8" />
          <meta name="viewport" content="width=device-width, initial-scale=1" />
          <title>Credentials Updated</title>
          <style>
            body {
              font-family: Arial, sans-serif;
              background: #f4f6f8;
              display: flex;
              justify-content: center;
              align-items: center;
              height: 100vh;
              margin: 0;
            }
            .container {
              background: white;
              padding: 30px;
              border-radius: 10px;
              box-shadow: 0 2px 12px rgba(0,0,0,0.1);
              text-align: center;
            }
            h2 {
              color: #28a745;
              margin-bottom: 20px;
            }
            button {
              background-color: #007bff;
              color: white;
              border: none;
              padding: 12px 24px;
              font-size: 16px;
              border-radius: 5px;
              cursor: pointer;
            }
            button:hover {
              background-color: #0056b3;
            }
          </style>
        </head>
        <body>
          <div class="container">
            <h2>✅ Credentials updated successfully!</h2>
            <button onclick="window.location.href='/'">Go to Home</button>
          </div>
        </body>
        </html>
      )rawliteral";

      webServer.send(200, "text/html; charset=utf-8", page);
    } else {
      webServer.send(400, "text/plain; charset=utf-8", "❌ Missing fields.");
    }
  });

  webServer.on("/api/door_status", HTTP_GET, handleApiStatus);

  webServer.on("/update_token", HTTP_POST, []() {
    if (webServer.hasArg("token")) {
      String newToken = webServer.arg("token");
      saveApiToken(newToken);

      String page = R"rawliteral(
        <!DOCTYPE html>
        <html lang="en">
        <head><meta charset="UTF-8"><meta name="viewport" content="width=device-width, initial-scale=1.0"><title>Token Updated</title></head>
        <body style="font-family: Arial, sans-serif; text-align:center; padding: 50px;">
          <h2>✅ API Token Updated Successfully!</h2>
          <p>New Token: <b>)rawliteral"
                    + newToken + R"rawliteral(</b></p>
          <button onclick="window.location.href='/'" style="padding:10px 20px; font-size:16px;">Go Back</button>
        </body>
        </html>
      )rawliteral";

      webServer.send(200, "text/html", page);
    } else {
      webServer.send(400, "text/plain", "Missing token parameter.");
    }
  });

  webServer.begin();
  Serial.println("HTTP server started on port 80");
}

void loop() {
  int pinState = digitalRead(resetPin);

  if (pinState == LOW) {
    if (resetButtonPressedTime == 0) {
      resetButtonPressedTime = millis();
    } else if (millis() - resetButtonPressedTime > 1000) {
      Serial.println("Reset button pressed for 1 second, clearing WiFi settings...");
      wm.resetSettings();
      ESP.restart();
    }
  } else {
    resetButtonPressedTime = 0;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected, trying to reconnect...");
    wm.autoConnect("DoorSensorHub", "password");
    return;
  }

  WiFiClient client = tcpServer.available();
  if (client) {
    Serial.println("Client connected");
    while (client.connected()) {
      if (client.available()) {
        String data = client.readStringUntil('\n');
        data.trim();
        if (data.length() > 0) {
          Serial.print("Received: ");
          Serial.println(data);

          handleMessage(data);

          struct tm timeinfo;
          getLocalTime(&timeinfo);

          char timestamp[20];
          strftime(timestamp, sizeof(timestamp), "%m/%d/%Y %H:%M:%S", &timeinfo);

          String formatted = String(timestamp) + " - " + data;

          if (messageCount < MAX_MESSAGES) {
            messages[messageCount++] = formatted;
          } else {
            for (int i = 1; i < MAX_MESSAGES; i++) {
              messages[i - 1] = messages[i];
            }
            messages[MAX_MESSAGES - 1] = formatted;
          }

          if (!isHome) {
            sendDiscordMessage(data);
          }

          int percentageUsed = (100 * messageCount) / MAX_MESSAGES;

          if (!warned70 && percentageUsed >= 70) {
            sendDiscordMessage("⚠️ Log usage has reached 70%. Please download logs soon.");
            warned70 = true;
          }

          if (!warned90 && percentageUsed >= 90) {
            sendDiscordMessage("🚨 Log usage has reached 90%! Download immediately.");
            warned90 = true;
          }
        }
      }
      webServer.handleClient();
    }
    Serial.println("Client disconnected");
    client.stop();
  }

  webServer.handleClient();

  checkSensorsHeartbeat();
}

void handleRoot() {
  String sunroomDoorState = "Unknown";
  String garageDoorState = "Unknown";
  String sunroomLastChanged = "Unknown";
  String garageDoorLastChanged = "Unknown";
  int sunroomIndex = findSensorIndex("Sunroom");
  int garageIndex = findSensorIndex("Garage");
  String sunroomStatusText = (sunroomIndex != -1 && sensorStatuses[sunroomIndex].isOnline) ? "🟢 Online" : "🔴 Offline";
  String garageStatusText = (garageIndex != -1 && sensorStatuses[garageIndex].isOnline) ? "🟢 Online" : "🔴 Offline";


  for (int i = messageCount - 1; i >= 0; i--) {
    String msg = messages[i];

    if (msg.indexOf("Sunroom Door") != -1 && sunroomDoorState == "Unknown") {
      sunroomDoorState = msg.indexOf("opened") != -1 ? "Opened" : "Closed";

      int lastSpace = msg.lastIndexOf(' ');
      int secondLastSpace = msg.lastIndexOf(' ', lastSpace - 1);
      if (secondLastSpace != -1 && lastSpace != -1) {
        String possibleTimestamp = msg.substring(secondLastSpace + 1);
        if (isValidTimestamp(possibleTimestamp)) {
          sunroomLastChanged = formatTimestamp(possibleTimestamp);
        } else {
          sunroomLastChanged = "Invalid Timestamp";
        }
      }
    }

    if (msg.indexOf("Garage Door") != -1 && garageDoorState == "Unknown") {
      garageDoorState = msg.indexOf("opened") != -1 ? "Opened" : "Closed";

      int lastSpace = msg.lastIndexOf(' ');
      int secondLastSpace = msg.lastIndexOf(' ', lastSpace - 1);
      if (secondLastSpace != -1 && lastSpace != -1) {
        String possibleTimestamp = msg.substring(secondLastSpace + 1);
        if (isValidTimestamp(possibleTimestamp)) {
          garageDoorLastChanged = formatTimestamp(possibleTimestamp);
        } else {
          garageDoorLastChanged = "Invalid Timestamp";
        }
      }
    }

    if (sunroomDoorState != "Unknown" && garageDoorState != "Unknown") break;
  }

  String page = "<!DOCTYPE html>";
  page += "<html lang='en'>";
  page += "<head>";
  page += "<meta charset='UTF-8'>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<h1>Door Status Log</h1>";

  page += "<div style='display: flex; justify-content: center; gap: 40px; margin-top: 20px; flex-wrap: wrap;'>";

  page += "<div style='padding: 15px 25px; background-color: #ffffff; border-left: 6px solid #007bff; border-radius: 10px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); text-align: center; width: 140px;'>";
  page += "<div style='font-weight: 700; font-size: 1.1em; margin-bottom: 10px;'>Sunroom Door</div>";
  page += "<div style='font-size: 3em; line-height: 1; margin-bottom: 10px;'>";
  page += sunroomDoorState == "Opened" ? "🔴 🚪" : "🟢 🚪";
  page += "</div>";
  page += "<div style='font-size: 1.2em; color: #34495e; margin-bottom: 5px;'>";
  page += sunroomDoorState == "Opened" ? "Opened" : "Closed";
  page += "</div>";
  page += "<div style='font-size: 0.9em; color: #7f8c8d;'>";
  page += "Last changed: " + sunroomLastChanged;
  page += "</div>";
  page += "<div style='font-size: 0.9em; color: #7f8c8d; margin-top: 5px;'>";
  page += sunroomStatusText;
  page += "</div>";
  page += "</div>";

  page += "<div style='padding: 15px 25px; background-color: #ffffff; border-left: 6px solid #28a745; border-radius: 10px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); text-align: center; width: 140px;'>";
  page += "<div style='font-weight: 700; font-size: 1.1em; margin-bottom: 10px;'>Garage Door</div>";
  page += "<div style='font-size: 3em; line-height: 1; margin-bottom: 10px;'>";
  page += garageDoorState == "Opened" ? "🔴 🚪" : "🟢 🚪";
  page += "</div>";
  page += "<div style='font-size: 1.2em; color: #34495e; margin-bottom: 5px;'>";
  page += garageDoorState == "Opened" ? "Opened" : "Closed";
  page += "</div>";
  page += "<div style='font-size: 0.9em; color: #7f8c8d;'>";
  page += "Last changed: " + garageDoorLastChanged;
  page += "</div>";
  page += "<div style='font-size: 0.9em; color: #7f8c8d; margin-top: 5px;'>";
  page += garageStatusText;
  page += "</div>";
  page += "</div>";

  page += "</div>";

  page += "<div style='text-align: center; margin-top: 20px;'>";
  page += "<p style='margin-bottom: 8px; font-weight: bold;'>Current Notification Mode:</p>";
  page += "<form action='/toggle_home' method='POST'>";
  page += "<button type='submit' style='background-color: " + String(isHome ? "#6c757d" : "#17a2b8") + "; color: white; padding: 10px 20px; border: none; border-radius: 8px; cursor: pointer;'>";
  page += isHome ? "🏠 Home (Notifications Off)" : "🚗 Away (Notifications On)";
  page += "</button>";
  page += "<p style='margin-top: 8px; color: #555;'>Tap to switch modes</p>";
  page += "</form></div>";

  page += "<link href='https://fonts.googleapis.com/css2?family=Roboto:wght@300;400;700&display=swap' rel='stylesheet'>";
  page += "<style>";

  page += "body { font-family: 'Roboto', sans-serif; margin: 0; padding: 0; background-color: #eef2f7; color: #34495e; line-height: 1.6; }";
  page += ".container { max-width: 800px; margin: 20px auto; padding: 25px; background-color: #ffffff; border-radius: 12px; box-shadow: 0 6px 20px rgba(0, 0, 0, 0.08); }";
  page += "h1 { font-size: 2.5em; color: #2c3e50; text-align: center; margin-bottom: 25px; font-weight: 700; }";
  page += "h3 { font-size: 1.3em; color: #34495e; margin-top: 25px; margin-bottom: 15px; font-weight: 500; text-align: center; }";
  page += "a { text-decoration: none; }";

  page += ".button-group { display: flex; justify-content: center; gap: 15px; margin-bottom: 30px; flex-wrap: wrap; }";
  page += "button {";
  page += "  background-color: #007bff; color: white; border: none; padding: 12px 25px; border-radius: 8px;";
  page += "  cursor: pointer; font-size: 1.05em; font-weight: 500; transition: all 0.3s ease; outline: none;";
  page += "  box-shadow: 0 4px 10px rgba(0, 123, 255, 0.2);";
  page += "}";
  page += "button:hover { background-color: #0056b3; transform: translateY(-2px); box-shadow: 0 6px 15px rgba(0, 123, 255, 0.3); }";
  page += "button:active { transform: translateY(0); box-shadow: 0 2px 5px rgba(0, 123, 255, 0.2); }";

  page += "ul { list-style-type: none; padding: 0; margin-top: 20px; }";
  page += "li { background: #f9f9fb; margin-bottom: 10px; padding: 15px 20px; border-radius: 10px; box-shadow: 0 2px 8px rgba(0,0,0,0.05); border-left: 5px solid #007bff; transition: transform 0.2s ease; }";
  page += "li:hover { transform: translateX(5px); }";
  page += "li:last-child { margin-bottom: 0; }";
  page += ".no-messages { text-align: center; color: #7f8c8d; padding: 20px; background-color: #f0f4f7; border-radius: 8px; margin-top: 20px; }";

  page += ".bar-container { width: 100%; background-color: #e0e6ed; border-radius: 25px; overflow: hidden; height: 35px; margin-bottom: 30px; box-shadow: inset 0 2px 5px rgba(0,0,0,0.1); }";
  page += ".bar-fill { height: 100%; text-align: center; color: white; font-weight: bold; line-height: 35px; border-radius: 25px; transition: width 0.7s ease-in-out, background-color 0.7s ease-in-out; display: flex; align-items: center; justify-content: center; }";

  page += "@media (max-width: 600px) {";
  page += "  .container { margin: 10px; padding: 15px; border-radius: 8px; }";
  page += "  h1 { font-size: 2em; margin-bottom: 20px; }";
  page += "  h3 { font-size: 1.1em; margin-top: 15px; }";
  page += "  .button-group { flex-direction: column; gap: 10px; margin-bottom: 20px; }";
  page += "  button { width: 100%; padding: 10px 15px; font-size: 1em; }";
  page += "  .bar-container { height: 30px; margin-bottom: 25px; }";
  page += "  .bar-fill { line-height: 30px; }";
  page += "  li { padding: 12px 15px; border-radius: 8px; border-left-width: 4px; }";
  page += "}";
  page += "</style>";
  page += "</head>";
  page += "<body>";

  page += R"rawliteral(
  <script>
    if (window.location.hostname.endsWith(".ngrok-free.app")) {
      fetch(window.location.href, {
        method: "GET",
        headers: {
          "ngrok-skip-browser-warning": "true"
        }
      })
      .then(resp => resp.text())
      .then(html => {
        document.open();
        document.write(html);
        document.close();
      });
    }
  </script>
  )rawliteral";

  page += "<div class='container'>";
  page += "<h1>Door Status Log</h1>";

  page += "<div class='button-group'>";
  page += R"rawliteral(
    <a href="/download" download="door_log.csv">
      <button type="button">Download CSV</button>
    </a>
    <a href="/clear">
      <button type="button">Clear Logs</button>
    </a>
    <a href="/">
      <button type="button">🔄 Refresh</button>
    </a>
  )rawliteral";
  page += "</div>";

  int filteredCount = 0;
  for (int i = 0; i < messageCount; i++) {
    String msg = messages[i];
    bool skip = false;

    for (int s = 0; s < MAX_SENSORS; s++) {
      String sensorConnected = sensorNames[s] + " sensor connected";
      String msgLower = msg;
      msgLower.toLowerCase();
      String targetLower = sensorConnected;
      targetLower.toLowerCase();
      if (msgLower.indexOf(targetLower) != -1) {
        skip = true;
        break;
      }
    }

    if (!skip) {
      filteredCount++;
    }
  }

  page += "<h3>Log Usage: " + String(filteredCount) + "/" + String(MAX_MESSAGES) + "</h3>";

  String barColor = "#28a745";
  if (filteredCount > (MAX_MESSAGES * 0.85)) {
    barColor = "#dc3545";
  } else if (filteredCount > (MAX_MESSAGES * 0.65)) {
    barColor = "#ffc107";
  }

  int percentage = (100 * filteredCount) / MAX_MESSAGES;
  int displayPercentage = (percentage > 100) ? 100 : percentage;
  page += "<div class='bar-container'><div class='bar-fill' style='width:" + String(displayPercentage) + "%; background-color:" + barColor + ";'>" + String(percentage) + "%</div></div>";

  page += R"rawliteral(
    <form action="/update_webhook" method="POST" style="margin-top: 20px;">
      <label for="webhook" style="font-weight: bold;">Change Discord Webhook:</label><br><br>
      <input type="text" id="webhook" name="webhook" value=")rawliteral"
          + webhook_url + R"rawliteral(" 
            onclick="this.select()" 
            style="width: 100%; padding: 10px; border-radius: 6px; border: 1px solid #ccc;"><br><br>
      <button type="submit" style="background-color: #28a745; color: white; border: none; padding: 10px 20px; border-radius: 8px; cursor: pointer;">Update Webhook</button>
    </form>
  )rawliteral";

  page += R"rawliteral(
    <style>
      .update-form {
        max-width: 400px;
        margin: 20px auto;
        padding: 20px;
        background: #f9f9f9;
        border-radius: 10px;
        box-shadow: 0 2px 8px rgba(0,0,0,0.1);
        font-family: Arial, sans-serif;
      }
      .update-form h3 {
        text-align: center;
        color: #333;
        margin-bottom: 20px;
      }
      .update-form label {
        display: block;
        font-weight: 600;
        margin-bottom: 6px;
        color: #555;
      }
      .update-form input[type="text"],
      .update-form input[type="password"] {
        width: 100%;
        padding: 10px;
        margin-bottom: 15px;
        border: 1px solid #ccc;
        border-radius: 6px;
        box-sizing: border-box;
        font-size: 1em;
      }
      .update-form input[type="submit"] {
        width: 100%;
        background-color: #007bff;
        color: white;
        padding: 12px;
        border: none;
        border-radius: 6px;
        font-size: 1em;
        cursor: pointer;
        transition: background-color 0.3s ease;
      }
      .update-form input[type="submit"]:hover {
        background-color: #0056b3;
      }
    </style>
    <style>
      input::placeholder {
        color: #aaa; /* light gray */
        font-style: italic;
      }
    </style>

    <div class="update-form">
      <h3>Update Username & Password</h3>
      <form action="/update_credentials" method="POST">
        <label for="username">Username:</label>
        <input
          type="text"
          id="username"
          name="username"
          required
          placeholder="Enter a new username"
        >
        <label for="password">Password:</label>
        <input
          type="password"
          id="password"
          name="password"
          required
          placeholder="Enter a new password"
        >
        <input type="submit" value="Update">
      </form>
    </div>
  )rawliteral";

  page += R"rawliteral(
    <div class="update-form">
      <h3>Update API Token</h3>
      <form action="/update_token" method="POST">
        <label for="token">API Token:</label>
        <input type="text" id="token" name="token" value=")rawliteral"
          + api_token + R"rawliteral(" required onclick="this.select();">
        <input type="submit" value="Update Token">
      </form>
    </div>
  )rawliteral";

  if (messageCount == 0) {
    page += "<p class='no-messages'>No messages received yet. The log will appear here when a door event occurs.</p>";
  } else {
    page += "<ul>";
    for (int i = 0; i < messageCount; i++) {
      String msg = messages[i];
      bool skip = false;

      for (int s = 0; s < MAX_SENSORS; s++) {
        String sensorConnected = sensorNames[s] + " sensor connected";

        String msgLower = msg;
        msgLower.toLowerCase();
        String targetLower = sensorConnected;
        targetLower.toLowerCase();

        if (msgLower.indexOf(targetLower) != -1) {
          skip = true;
          break;
        }
      }

      if (!skip) {
        page += "<li>" + msg + "</li>";
      }
    }
    page += "</ul>";
  }

  page += "</div>";
  page += "</body>";
  page += "</html>";
  webServer.send(200, "text/html", page);
}

void handleMessage(String message) {
  int spaceIndex = message.indexOf(' ');
  if (spaceIndex == -1) return;

  String sensorName = message.substring(0, spaceIndex);
  String rest = message.substring(spaceIndex + 1);

  if (rest == "sensor connected") {
    onHeartbeatReceived(sensorName);
  }
}

void onHeartbeatReceived(const String& sensorName) {
  Serial.println("Heartbeat received from sensor: " + sensorName);

  int idx = findSensorIndex(sensorName);
  if (idx == -1) {
    Serial.println("Unknown sensor: " + sensorName);
    return;
  }

  sensorStatuses[idx].lastHeartbeatTime = millis();

  if (!sensorStatuses[idx].isOnline) {
    Serial.println(sensorName + " sensor online");
    sensorStatuses[idx].isOnline = true;
  }
}

void checkSensorsHeartbeat() {
  unsigned long currentTime = millis();
  for (int i = 0; i < MAX_SENSORS; i++) {
    if (sensorNames[i].length() == 0) continue;

    if (sensorStatuses[i].isOnline && (currentTime - sensorStatuses[i].lastHeartbeatTime > HEARTBEAT_TIMEOUT_MINUTES * 60UL * 1000UL)) {
      Serial.println(sensorNames[i] + " sensor offline");
      sensorStatuses[i].isOnline = false;
    }
  }
}

void saveToFile(const char* path, const String& data) {
  File file = SPIFFS.open(path, "w");
  if (!file) return;
  file.print(data);
  file.close();
}

void saveApiToken(const String& token) {
  Serial.println("Saving API token: " + token);

  File file = SPIFFS.open("/api_token.txt", "w");
  if (!file) {
    Serial.println("Failed to open /api_token.txt for writing");
    return;
  }

  file.print(token);
  file.close();

  api_token = token;

  Serial.println("API token saved.");
}

void handleApiStatus() {
  if (!webServer.hasArg("token") || webServer.arg("token") != api_token) {
    webServer.send(401, "application/json", "{\"error\": \"Unauthorized\"}");
    return;
  }

  String sunroomDoorState = "Unknown";
  String garageDoorState = "Unknown";
  String sunroomLastChanged = "Unknown";
  String garageDoorLastChanged = "Unknown";

  int sunroomIndex = findSensorIndex("Sunroom");
  int garageIndex = findSensorIndex("Garage");

  String sunroomStatusText = (sunroomIndex != -1 && sensorStatuses[sunroomIndex].isOnline) ? "Online" : "Offline";
  String garageStatusText = (garageIndex != -1 && sensorStatuses[garageIndex].isOnline) ? "Online" : "Offline";

  for (int i = messageCount - 1; i >= 0; i--) {
    String msg = messages[i];
    if (msg.indexOf("Sunroom Door") != -1 && sunroomDoorState == "Unknown") {
      sunroomDoorState = msg.indexOf("opened") != -1 ? "Opened" : "Closed";
      int idx = msg.indexOf(" - ");
      if (idx != -1) {
        String ts = msg.substring(0, idx);
        if (isValidTimestamp(ts)) {
          sunroomLastChanged = formatTimestamp(ts);
        } else {
          sunroomLastChanged = "Invalid Timestamp";
        }
      }
    }

    if (msg.indexOf("Garage Door") != -1 && garageDoorState == "Unknown") {
      garageDoorState = msg.indexOf("opened") != -1 ? "Opened" : "Closed";
      int idx = msg.indexOf(" - ");
      if (idx != -1) {
        String ts = msg.substring(0, idx);
        if (isValidTimestamp(ts)) {
          garageDoorLastChanged = formatTimestamp(ts);
        } else {
          garageDoorLastChanged = "Invalid Timestamp";
        }
      }
    }

    if (sunroomDoorState != "Unknown" && garageDoorState != "Unknown") break;
  }

  String json = "{";
  json += "\"sunroom_door\": \"" + sunroomDoorState + "\", ";
  json += "\"sunroom_status\": \"" + sunroomStatusText + "\", ";
  json += "\"last_changed_sunroom_door\": \"" + sunroomLastChanged + "\", ";
  json += "\"garage_door\": \"" + garageDoorState + "\", ";
  json += "\"garage_status\": \"" + garageStatusText + "\", ";
  json += "\"last_changed_garage_door\": \"" + garageDoorLastChanged + "\", ";
  json += "\"notifications_enabled\": " + String(isHome ? "false" : "true");
  json += "}";

  webServer.send(200, "application/json", json);
}

void saveHomeStatus(bool home) {
  File file = SPIFFS.open(HOME_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open home file for writing");
    return;
  }
  file.print(home ? "1" : "0");
  file.close();

  String status = home ? "Home" : "Away";
  Serial.println("Home status saved: " + status);

  String message = "Home status saved: " + status;
  sendDiscordMessage(message);
}

void saveWebhookToFile(const String& url) {
  File file = SPIFFS.open(WEBHOOK_FILE, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open webhook file for writing");
    return;
  }
  file.print(url);
  file.close();
  Serial.println("Webhook saved to SPIFFS.");
}

void sendDiscordMessage(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;

    http.begin(webhook_url);
    http.addHeader("Content-Type", "application/json");

    String payload = "{\"content\":\"" + message + "\"}";

    int httpCode = http.POST(payload);

    if (httpCode > 0) {
      Serial.println("Discord message sent.");
    } else {
      Serial.printf("Discord message failed: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  }
}

void handleDownload() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    webServer.send(500, "text/plain", "Failed to get time.");
    return;
  }

  char dateStr[9];
  strftime(dateStr, sizeof(dateStr), "%m%d%Y", &timeinfo);

  String baseFilename = "door_sensor_logs_" + String(dateStr);
  String filename = baseFilename + ".csv";

  if (downloadCounter > 1) {
    filename = baseFilename + "_" + String(downloadCounter) + ".csv";
  }
  downloadCounter++;

  String csv = "Date,Time,Status\n";
  for (int i = 0; i < messageCount; i++) {
    String entry = messages[i];

    int dateEnd = entry.indexOf(' ');
    int timeEnd = entry.indexOf(" - ");

    if (dateEnd > 0 && timeEnd > dateEnd) {
      String date = entry.substring(0, dateEnd);
      String time = entry.substring(dateEnd + 1, timeEnd);
      String status = entry.substring(timeEnd + 3);

      csv += date + "," + time + "," + status + "\n";
    }
  }

  webServer.sendHeader("Content-Type", "text/csv");
  webServer.sendHeader("Content-Disposition", "attachment; filename=" + filename);
  webServer.sendHeader("Connection", "close");
  webServer.send(200, "text/csv", csv);
}

void handleClearForm() {
  String form = "<!DOCTYPE html>";
  form += "<html lang='en'>";
  form += "<head>";
  form += "<meta charset='UTF-8'>";
  form += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  form += "<title>Confirm Delete</title>";
  form += "<link href='https://fonts.googleapis.com/css2?family=Roboto:wght@300;400;700&display=swap' rel='stylesheet'>";
  form += "<style>";

  form += "body { font-family: 'Roboto', sans-serif; margin: 0; padding: 0; background-color: #eef2f7; color: #34495e; line-height: 1.6; display: flex; justify-content: center; align-items: center; min-height: 100vh; }";
  form += ".container { max-width: 500px; margin: 20px auto; padding: 25px; background-color: #ffffff; border-radius: 12px; box-shadow: 0 6px 20px rgba(0, 0, 0, 0.08); text-align: center; }";
  form += "h2 { font-size: 2em; color: #dc3545; margin-bottom: 20px; font-weight: 700; }";
  form += "p { font-size: 1.1em; margin-bottom: 25px; color: #555; }";
  form += "b { color: #2c3e50; }";

  form += "form { display: flex; flex-direction: column; align-items: center; }";
  form += "input[type='text'] {";
  form += "  width: calc(100% - 40px); padding: 12px; margin-bottom: 20px; border: 1px solid #ced4da; border-radius: 8px;";
  form += "  font-size: 1em; outline: none; transition: border-color 0.3s ease, box-shadow 0.3s ease; box-sizing: border-box;";
  form += "}";
  form += "input[type='text']:focus { border-color: #007bff; box-shadow: 0 0 0 0.2rem rgba(0, 123, 255, 0.25); }";

  form += "input[type='submit'] {";
  form += "  background-color: #dc3545; color: white; border: none; padding: 12px 30px; border-radius: 8px;";
  form += "  cursor: pointer; font-size: 1.1em; font-weight: 500; transition: all 0.3s ease; outline: none;";
  form += "  box-shadow: 0 4px 10px rgba(220, 53, 69, 0.2); margin-bottom: 20px;";
  form += "}";
  form += "input[type='submit']:hover { background-color: #c82333; transform: translateY(-2px); box-shadow: 0 6px 15px rgba(220, 53, 69, 0.3); }";
  form += "input[type='submit']:active { transform: translateY(0); box-shadow: 0 2px 5px rgba(220, 53, 69, 0.2); }";

  form += "@media (max-width: 600px) {";
  form += "  .container { margin: 10px; padding: 20px; border-radius: 8px; }";
  form += "  h2 { font-size: 1.8em; }";
  form += "  p { font-size: 1em; }";
  form += "  input[type='text'] { width: calc(100% - 30px); padding: 10px; }";
  form += "  input[type='submit'] { padding: 10px 20px; font-size: 1em; width: 100%; }";

  form += "}";
  form += "</style>";
  form += "</head>";
  form += "<body>";

  form += "<div class='container'>";
  form += "<h2>Confirm Deletion</h2>";
  form += "<p>To delete all logs, please type <b>Delete all logs</b> exactly below and press Confirm:</p>";
  form += "<form method='POST' action='/confirm_clear'>";
  form += "<input type='text' name='confirmation' autocomplete='off' placeholder='Type &quot;Delete all logs&quot; here'>";
  form += "<input type='submit' value='Confirm'>";
  form += "</form>";

  form += "</div>";
  form += "</body>";
  form += "</html>";

  webServer.send(200, "text/html", form);
}

void handleClearLogs() {
  String responsePage = "<!DOCTYPE html>";
  responsePage += "<html lang='en'>";
  responsePage += "<head>";
  responsePage += "<meta charset='UTF-8'>";
  responsePage += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  responsePage += "<title>Log Clear Status</title>";
  responsePage += "<link href='https://fonts.googleapis.com/css2?family=Roboto:wght@300;400;700&display=swap' rel='stylesheet'>";
  responsePage += "<style>";
  responsePage += "body { font-family: 'Roboto', sans-serif; margin: 0; padding: 0; background-color: #eef2f7; color: #34495e; line-height: 1.6; display: flex; justify-content: center; align-items: center; min-height: 100vh; }";
  responsePage += ".container { max-width: 500px; margin: 20px auto; padding: 25px; background-color: #ffffff; border-radius: 12px; box-shadow: 0 6px 20px rgba(0, 0, 0, 0.08); text-align: center; }";
  responsePage += "h2 { font-size: 2em; margin-bottom: 20px; font-weight: 700; }";
  responsePage += ".success { color: #28a745; }";
  responsePage += ".error { color: #dc3545; }";
  responsePage += "a {";
  responsePage += "  display: inline-block; margin-top: 25px; background-color: #007bff; color: white; padding: 10px 20px;";
  responsePage += "  border-radius: 8px; text-decoration: none; font-weight: 500; transition: all 0.3s ease;";
  responsePage += "  box-shadow: 0 4px 10px rgba(0, 123, 255, 0.2);";
  responsePage += "}";
  responsePage += "a:hover { background-color: #0056b3; transform: translateY(-2px); box-shadow: 0 6px 15px rgba(0, 123, 255, 0.3); }";
  responsePage += "a:active { transform: translateY(0); box-shadow: 0 2px 5px rgba(0, 123, 255, 0.2); }";

  responsePage += "@media (max-width: 600px) {";
  responsePage += "  .container { margin: 10px; padding: 20px; border-radius: 8px; }";
  responsePage += "  h2 { font-size: 1.8em; }";
  responsePage += "  a { padding: 8px 15px; font-size: 0.9em; }";
  responsePage += "}";
  responsePage += "</style>";
  responsePage += "</head>";
  responsePage += "<body>";

  responsePage += "<div class='container'>";
  if (webServer.hasArg("confirmation")) {
    String input = webServer.arg("confirmation");
    if (input == "Delete all logs") {
      messageCount = 0;
      warned70 = false;
      warned90 = false;
      responsePage += "<h2 class='success'>Logs cleared successfully.</h2>";
    } else {
      responsePage += "<h2 class='error'>Incorrect input. Logs were not cleared.</h2>";
    }
  } else {
    responsePage += "<h2 class='error'>No confirmation input provided. Logs were not cleared.</h2>";
  }
  responsePage += "<a href='/'>Go Back</a>";
  responsePage += "</div>";

  responsePage += "</body>";
  responsePage += "</html>";
  webServer.send(200, "text/html", responsePage);
}