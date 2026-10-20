#include <WiFi.h>
#include <HTTPClient.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

WiFiServer server(80);

const char* lightsOnURL = "http://192.168.1.87/?led=on";
const char* lightsOffURL = "http://192.168.1.87/?led=off";
const char* bothBlindsOpenURL = "http://192.168.1.65/runBothForward";
const char* bothBlindsCloseURL = "http://192.168.1.65/runBothReverse";
const char* leftBlindsOpenURL = "http://192.168.1.65/runMotorAForward";
const char* leftBlindsCloseURL = "http://192.168.1.65/runMotorAReverse";
const char* rightBlindsOpenURL = "http://192.168.1.65/runMotorBForward";
const char* rightBlindsCloseURL = "http://192.168.1.65/runMotorBReverse";
const char* blindsURL = "http://192.168.1.65";
const char* lightsURL = "http://192.168.1.87";  // new: to fetch light state

void sendCommand(String url) {
  HTTPClient http;
  http.begin(url);
  int httpCode = http.GET();
  http.end();
}

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected! IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();
}

void loop() {
  WiFiClient client = server.available();
  if (client) {
    Serial.println("Client connected");
    String request = client.readStringUntil('\r');
    Serial.println(request);
    client.flush();

    if (request.indexOf("/api?cmd=") != -1) {
      if (request.indexOf("lightsOn") != -1) sendCommand(lightsOnURL);
      if (request.indexOf("lightsOff") != -1) sendCommand(lightsOffURL);
      if (request.indexOf("bothBlindsOpen") != -1) sendCommand(bothBlindsOpenURL);
      if (request.indexOf("bothBlindsClose") != -1) sendCommand(bothBlindsCloseURL);
      if (request.indexOf("leftBlindsOpen") != -1) sendCommand(leftBlindsOpenURL);
      if (request.indexOf("leftBlindsClose") != -1) sendCommand(leftBlindsCloseURL);
      if (request.indexOf("rightBlindsOpen") != -1) sendCommand(rightBlindsOpenURL);
      if (request.indexOf("rightBlindsClose") != -1) sendCommand(rightBlindsCloseURL);
      
      client.println("HTTP/1.1 200 OK");
      client.println("Content-Type: text/plain");
      client.println();
      client.println("OK");
      client.stop();
      return;
    }

    // Fetch blinds state
    String motorAState = "";
    String motorBState = "";
    HTTPClient http;
    http.begin(blindsURL);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      motorAState = getMotorState(payload, 0);
      motorBState = getMotorState(payload, 1);
    } else {
      motorAState = "Error";
      motorBState = "Error";
    }
    http.end();

    // Fetch light state
    String lightState = "";
    http.begin(lightsURL);
    httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      lightState = getLightState(payload);
    } else {
      lightState = "Error";
    }
    http.end();

    // Generate webpage
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();

    client.println(R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>ESP32 Automation Hub</title>
<style>
  body { background: #f5f5f5; font-family: Arial, sans-serif; text-align: center; margin: 0; padding: 20px; }
  .container { background: #fff; padding: 20px; border-radius: 10px; box-shadow: 0 2px 8px rgba(0,0,0,0.2); display: inline-block; }
  h1 { color: #333; }
  h2 { color: #555; border-bottom: 1px solid #ddd; padding-bottom: 10px; margin-bottom: 20px; }
  .button { margin: 10px; padding: 15px 30px; border: none; border-radius: 5px; font-size: 16px; cursor: pointer; width: 180px; }
  .on { background: #4CAF50; color: white; }
  .off { background: #f44336; color: white; }
  .neutral { background: #3498db; color: white; }
  .grid { display: grid; grid-template-columns: 1fr 1fr; gap: 10px; justify-items: center; }
</style>
<script>
function sendCommand(cmd) {
  fetch(`/api?cmd=${cmd}`)
    .then(res => console.log('Command sent:', cmd));
}
</script>
</head>
<body>
<div class="container">
  <h1>ESP32 Automation Hub</h1>

  <h2>Lights Control</h2>
  <button class="button on" onclick="sendCommand('lightsOn')">Lights On</button>
  <button class="button off" onclick="sendCommand('lightsOff')">Lights Off</button>

  <h3>Light State: )rawliteral");
    client.print(lightState);
    client.println("</h3>");

    client.println(R"rawliteral(
  <h2>Blinds Control</h2>
  <div class="grid">
    <div><button class="button neutral" onclick="sendCommand('bothBlindsOpen')">Both Open</button></div>
    <div><button class="button off" onclick="sendCommand('bothBlindsClose')">Both Close</button></div>

    <div><button class="button neutral" onclick="sendCommand('leftBlindsOpen')">Left Open</button></div>
    <div><button class="button off" onclick="sendCommand('leftBlindsClose')">Left Close</button></div>

    <div><button class="button neutral" onclick="sendCommand('rightBlindsOpen')">Right Open</button></div>
    <div><button class="button off" onclick="sendCommand('rightBlindsClose')">Right Close</button></div>
  </div>
)rawliteral");

    client.println("<h3>Left Blinds: " + motorAState + "</h3>");
    client.println("<h3>Right Blinds: " + motorBState + "</h3>");
    client.println("</div></body></html>");
    client.println();
    client.stop();
    Serial.println("Client disconnected");
  }
}

String getMotorState(String payload, int occurrence) {
  int pos = 0;
  for (int i = 0; i <= occurrence; i++) {
    pos = payload.indexOf("Current State: ", pos + 1);
    if (pos == -1) return "Unknown";
  }
  int start = pos + 15;
  int end = payload.indexOf("</p>", start);
  return payload.substring(start, end);
}

String getLightState(String payload) {
  int pos = payload.indexOf("LED is currently <strong>");
  if (pos == -1) return "Unknown";
  int start = pos + 25;
  int end = payload.indexOf("</strong>", start);
  return payload.substring(start, end);
}