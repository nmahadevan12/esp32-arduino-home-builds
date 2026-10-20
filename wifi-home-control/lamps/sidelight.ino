#include <WiFi.h>
#include <time.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

WiFiServer server(80);

#define RELAY_PIN 13

// Eastern Time (handles daylight savings)
const long gmtOffset_sec = -5 * 3600;
const int daylightOffset_sec = 3600;

// Track if the daily ON/OFF message has already been printed
bool lightOnLogged = false;
bool lightOffLogged = false;

const char MAIN_page[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
body { font-family: Arial; text-align:center; background:#f0f0f0; padding:20px; }
button {
  background:#007BFF; color:white; padding:15px 40px; 
  font-size:18px; border:none; border-radius:8px; cursor:pointer;
}
button:hover { background:#0056b3; }
a { text-decoration:none; }
</style>
</head>
<body>
<h1>ESP32 Light Control</h1>
<p><a href="/on"><button>Turn ON</button></a></p>
<p><a href="/off"><button>Turn OFF</button></a></p>
</body>
</html>
)rawliteral";

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);  // OFF initially (active LOW)

  Serial.begin(115200);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nConnected!");
  Serial.println(WiFi.localIP());

  // ---- FIXED NTP SYNC ----
  configTime(gmtOffset_sec, daylightOffset_sec,
             "pool.ntp.org",
             "time.google.com",
             "time.nist.gov");

  Serial.println("Syncing time...");
  struct tm timeinfo;
  int retry = 0;
  const int maxRetries = 20;

  while (!getLocalTime(&timeinfo) && retry < maxRetries) {
    retry++;
    Serial.println("Waiting for NTP time...");
    delay(1000);
  }

  if (retry == maxRetries) {
    Serial.println("ERROR: Failed to sync NTP time.");
  } else {
    Serial.println("Time synchronized successfully!");
  }

  server.begin();
}

// ---------------------- AUTO TIMER ----------------------
void handleTimedRelay() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  int hour = timeinfo.tm_hour;
  int minute = timeinfo.tm_min;

  // Reset the flags at midnight
  if (hour == 0 && minute == 0) {
    lightOnLogged = false;
    lightOffLogged = false;
  }

  // ----- ON at 6:10 PM (18:10) -----
  if (hour == 18 && minute == 10) {
    digitalWrite(RELAY_PIN, HIGH);

    if (!lightOnLogged) {
      Serial.println("AUTO: Light turned ON at 6:10 PM");
      lightOnLogged = true;
    }
  }

  // ----- OFF at 9:30 PM (21:30) -----
  if (hour == 21 && minute == 30) {
    digitalWrite(RELAY_PIN, LOW);

    if (!lightOffLogged) {
      Serial.println("AUTO: Light turned OFF at 9:30 PM");
      lightOffLogged = true;
    }
  }
}

void loop() {
  handleTimedRelay();

  WiFiClient client = server.available();
  if (!client) return;

  Serial.println("Client connected");

  String req = client.readStringUntil('\r');
  client.flush();

  if (req.indexOf("GET /off") >= 0) {
    digitalWrite(RELAY_PIN, LOW);
    Serial.println("MANUAL: Light turned OFF");
  }
  if (req.indexOf("GET /on") >= 0) {
    digitalWrite(RELAY_PIN, HIGH);
    Serial.println("MANUAL: Light turned ON");
  }

  client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n");
  client.print(MAIN_page);

  delay(1);
  client.stop();
  Serial.println("Client disconnected");
}
