#include <WiFi.h>
#include <WiFiManager.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <time.h>
#include <FS.h>
#include <SPIFFS.h>
#include "mbedtls/sha256.h"
#include "mbedtls/base64.h"

const int resetPin = 13;

String webhook_url = "";

const char* stored_username_hash = "REPLACE_WITH_SHA256_OF_USERNAME";
const char* stored_password_hash = "REPLACE_WITH_SHA256_OF_PASSWORD";

WiFiServer tcpServer(1234);
WebServer webServer(80);

#define MAX_MESSAGES 80
String messages[MAX_MESSAGES];
int messageCount = 0;

int downloadCounter = 1;

unsigned long sessionStart = 0;
const unsigned long sessionTimeout = 30UL * 60UL * 1000UL;

bool warned70 = false;
bool warned90 = false;

unsigned int failedAttempts = 0;
unsigned long lockoutEndTime = 0;

bool lockoutMessageSent = false;

WiFiManager wm;

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

void setup() {
  pinMode(resetPin, INPUT_PULLUP);

  Serial.begin(115200);
  WiFiManager wm;
  bool res;
  res = wm.autoConnect("DoorSensorHub", "password");

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

  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount SPIFFS");
  } else {
    File file = SPIFFS.open("/webhook.txt");
    if (file) {
      webhook_url = file.readStringUntil('\n');
      file.close();
      Serial.println("Loaded webhook: " + webhook_url);
    } else {
      Serial.println("No saved webhook.");
    }
  }

  configTime(-4 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  tcpServer.begin();
  Serial.println("TCP server started on port 1234");

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

  webServer.on("/setwebhook", HTTP_POST, handleSetWebhook);

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

  webServer.begin();
  Serial.println("HTTP server started on port 80");
}

void loop() {
  if (digitalRead(resetPin) == LOW) {
    Serial.println("Reset button pressed, clearing WiFi settings...");
    delay(1000);
    wm.resetSettings();
    ESP.restart();
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

          sendDiscordMessage(data);

          int percentageUsed = (100 * messageCount) / MAX_MESSAGES;

          if (!warned70 && percentageUsed >= 56) {
            sendDiscordMessage("⚠️ Log usage has reached 70%. Please download logs soon.");
            warned70 = true;
          }

          if (!warned90 && percentageUsed >= 72) {
            sendDiscordMessage("🚨 Log usage has reached 90%! Download immediately.");
            warned90 = true;
          }
        }
      }
    }
    Serial.println("Client disconnected");
    client.stop();
  }

  webServer.handleClient();
}

void handleRoot() {
  String frontDoorState = "Unknown";
  String backDoorState = "Unknown";

  for (int i = messageCount - 1; i >= 0; i--) {
    if (messages[i].indexOf("Front Door") != -1 && frontDoorState == "Unknown") {
      frontDoorState = messages[i].indexOf("opened") != -1 ? "Opened" : "Closed";
    }
    if (messages[i].indexOf("Back Door") != -1 && backDoorState == "Unknown") {
      backDoorState = messages[i].indexOf("opened") != -1 ? "Opened" : "Closed";
    }
    if (frontDoorState != "Unknown" && backDoorState != "Unknown") break;
  }

  String page = "<!DOCTYPE html>";
  page += "<html lang='en'>";
  page += "<head>";
  page += "<meta charset='UTF-8'>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<h1>Door Status Log</h1>";
  page += "<h3>Front Door: " + frontDoorState + " | Back Door: " + backDoorState + "</h3>";
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

  page += "<div class='container'>";
  page += "<h1>Door Status Log</h1>";

  page += "<div class='button-group'>";
  page += R"rawliteral(
    <a href="/download" download="door_log.csv">
      <button type="button">Download CSV</button>
    </a>
  )rawliteral";
  page += "<a href=\"/clear\"><button>Clear Logs</button></a>";
  page += "</div>";

  page += "<h3>Log Usage: " + String(messageCount) + "/" + String(MAX_MESSAGES) + "</h3>";

  page += R"rawliteral(
    <form action="/setwebhook" method="POST" style="margin-bottom:20px; text-align:center;">
      <label for="webhook">Set Discord Webhook URL:</label><br>
      <input type="text" id="webhook" name="webhook" value=")rawliteral" + webhook_url + R"rawliteral(" style="width:90%; padding:14px 10px; font-size: 1.1em; border-radius:8px; border:1px solid #ccc; margin:10px 0;">
      <br>
      <input type="submit" value="Save Webhook" style="background-color:#28a745; color:white; padding:14px 40px; font-size:1.3em; border:none; border-radius:6px; cursor:pointer;">
    </form>
  )rawliteral";

  String barColor = "#28a745";
  if (messageCount > (MAX_MESSAGES * 0.85)) {
    barColor = "#dc3545";
  } else if (messageCount > (MAX_MESSAGES * 0.65)) {
    barColor = "#ffc107";
  }

  int percentage = (100 * messageCount) / MAX_MESSAGES;
  int displayPercentage = (percentage > 100) ? 100 : percentage;
  page += "<div class='bar-container'><div class='bar-fill' style='width:" + String(displayPercentage) + "%; background-color:" + barColor + ";'>" + String(percentage) + "%</div></div>";

  if (messageCount == 0) {
    page += "<p class='no-messages'>No messages received yet. The log will appear here when a door event occurs.</p>";
  } else {
    page += "<ul>";
    for (int i = 0; i < messageCount; i++) {
      page += "<li>" + messages[i] + "</li>";
    }
    page += "</ul>";
  }

  page += "</div>";
  page += "</body>";
  page += "</html>";
  webServer.send(200, "text/html", page);
}

void handleSetWebhook() {
  String page = "<!DOCTYPE html><html lang='en'><head>";
  page += "<meta charset='UTF-8'>";
  page += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  page += "<title>Webhook Saved</title>";
  page += "<link href='https://fonts.googleapis.com/css2?family=Roboto:wght@300;400;700&display=swap' rel='stylesheet'>";
  page += "<style>";
  page += "body { font-family: 'Roboto', sans-serif; background: #eef2f7; color: #34495e; text-align: center; padding: 40px; }";
  page += ".container { background: white; max-width: 600px; margin: auto; padding: 30px; border-radius: 12px; box-shadow: 0 6px 20px rgba(0,0,0,0.08); }";
  page += "h1 { color: #28a745; font-weight: 700; }";
  page += "p { font-size: 1.2em; margin: 20px 0; }";
  page += "button { background-color: #007bff; color: white; border: none; padding: 12px 25px; border-radius: 8px; cursor: pointer; font-size: 1.1em; }";
  page += "button:hover { background-color: #0056b3; }";
  page += "</style>";
  page += "</head><body><div class='container'>";

  if (webServer.hasArg("webhook")) {
    webhook_url = webServer.arg("webhook");
    Serial.println("New webhook URL set: " + webhook_url);
    saveWebhook();

    page += "<h1>Webhook Saved!</h1>";
    page += "<p>Your new Discord webhook URL has been saved successfully.</p>";

    page += "<div style='word-wrap: break-word; max-width: 100%; padding: 10px; ";
    page += "background: #f9f9f9; border-radius: 6px; border: 1px solid #ccc; margin: 10px auto;'>";
    page += "<strong>" + webhook_url + "</strong></div>";
  } else {
      page += "<h1>Error</h1><p>No webhook URL provided.</p>";
  }

  page += "<button onclick=\"window.location.href='/'\">Return Home</button>";
  page += "</div></body></html>";

  webServer.send(200, "text/html", page);
}

void saveWebhook() {
  File file = SPIFFS.open("/webhook.txt", FILE_WRITE);
  if (file) {
    file.print(webhook_url);
    file.close();
  }
}

void loadWebhook() {
  File file = SPIFFS.open("/webhook.txt");
  if (file) {
    webhook_url = file.readStringUntil('\n');
    file.close();
  }
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