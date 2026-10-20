#include <WiFi.h>

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

WiFiServer server(80);

const int IN1 = 18;
const int IN2 = 19;
const int IN3 = 22;
const int IN4 = 23;

String motorAState = "Open";
String motorBState = "Open";

void setup() {
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, LOW);
  Serial.begin(115200);
  Serial.println();
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected to WiFi. IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();
}

void runBothForward() {
  Serial.println("Both Motors Forward for 1 second");
  digitalWrite(IN2, LOW);
  digitalWrite(IN4, LOW);
  digitalWrite(IN1, HIGH);
  digitalWrite(IN3, HIGH);
  delay(1000);
  digitalWrite(IN1, LOW);
  digitalWrite(IN3, LOW);
  motorAState = "Open";
  motorBState = "Open";
}

void runBothReverse() {
  Serial.println("Both Motors Reverse for 1 second");
  digitalWrite(IN1, LOW);
  digitalWrite(IN3, LOW);
  digitalWrite(IN2, HIGH);
  digitalWrite(IN4, HIGH);
  delay(1000);
  digitalWrite(IN2, LOW);
  digitalWrite(IN4, LOW);
  motorAState = "Closed";
  motorBState = "Closed";
}

void runMotorAForward() {
  Serial.println("Motor A Forward for 1 second");
  digitalWrite(IN2, LOW);
  digitalWrite(IN1, HIGH);
  delay(1000);
  digitalWrite(IN1, LOW);
  motorAState = "Open";
}

void runMotorAReverse() {
  Serial.println("Motor A Reverse for 1 second");
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  delay(1000);
  digitalWrite(IN2, LOW);
  motorAState = "Closed";
}

void runMotorBForward() {
  Serial.println("Motor B Forward for 1 second");
  digitalWrite(IN4, LOW);
  digitalWrite(IN3, HIGH);
  delay(1000);
  digitalWrite(IN3, LOW);
  motorBState = "Open";
}

void runMotorBReverse() {
  Serial.println("Motor B Reverse for 1 second");
  digitalWrite(IN3, LOW);
  digitalWrite(IN4, HIGH);
  delay(1000);
  digitalWrite(IN4, LOW);
  motorBState = "Closed";
}

void loop() {
  WiFiClient client = server.available();

  if (client) {
    Serial.println("New Client connected");
    String request = client.readStringUntil('\r');
    Serial.println("Request: " + request);
    client.flush();

    if (request.indexOf("/runBothForward") != -1) {
      if (motorAState != "Open" || motorBState != "Open") {
        runBothForward();
      } else {
        Serial.println("Both already open. No movement.");
      }
    }
    else if (request.indexOf("/runBothReverse") != -1) {
      if (motorAState != "Closed" || motorBState != "Closed") {
        runBothReverse();
      } else {
        Serial.println("Both already closed. No movement.");
      }
    }
    else if (request.indexOf("/runMotorAForward") != -1) {
      if (motorAState != "Open") {
        runMotorAForward();
      } else {
        Serial.println("Motor A already open. No movement.");
      }
    }
    else if (request.indexOf("/runMotorAReverse") != -1) {
      if (motorAState != "Closed") {
        runMotorAReverse();
      } else {
        Serial.println("Motor A already closed. No movement.");
      }
    }
    else if (request.indexOf("/runMotorBForward") != -1) {
      if (motorBState != "Open") {
        runMotorBForward();
      } else {
        Serial.println("Motor B already open. No movement.");
      }
    }
    else if (request.indexOf("/runMotorBReverse") != -1) {
      if (motorBState != "Closed") {
        runMotorBReverse();
      } else {
        Serial.println("Motor B already closed. No movement.");
      }
    }

    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
    client.println("<!DOCTYPE html><html>");
    client.println("<head><title>ESP32 Blinds Control</title>");
    client.println("<style>");
    client.println("body { background-color: #121212; color: white; font-family: Arial, sans-serif; text-align: center; }");
    client.println("h1 { color: #00bcd4; }");
    client.println(".section { border: 1px solid #333; border-radius: 15px; margin: 20px; padding: 20px; background-color: #1e1e1e; }");
    client.println("button { padding: 15px 30px; font-size: 18px; margin: 10px; border-radius: 10px; border: none; }");
    client.println(".forward { background-color: #4CAF50; color: white; }");
    client.println(".reverse { background-color: #f44336; color: white; }");
    client.println("</style>");
    client.println("</head>");
    client.println("<body>");
    client.println("<h1>ESP32 Blinds Control Panel</h1>");

    client.println("<div class='section'><h2>Both Blinds Control</h2>");
    client.println("<a href=\"/runBothForward\"><button class='forward'>Open 1s</button></a>");
    client.println("<a href=\"/runBothReverse\"><button class='reverse'>Close 1s</button></a></div>");

    client.println("<div class='section'><h2>Left Blinds Control</h2>");
    client.println("<a href=\"/runMotorAForward\"><button class='forward'>Open 1s</button></a>");
    client.println("<a href=\"/runMotorAReverse\"><button class='reverse'>Close 1s</button></a>");
    client.println("<p>Current State: " + motorAState + "</p></div>");

    client.println("<div class='section'><h2>Right Blinds Control</h2>");
    client.println("<a href=\"/runMotorBForward\"><button class='forward'>Open 1s</button></a>");
    client.println("<a href=\"/runMotorBReverse\"><button class='reverse'>Close 1s</button></a>");
    client.println("<p>Current State: " + motorBState + "</p></div>");

    client.println("</body></html>");
    client.println();

    delay(1);
    client.stop();
    Serial.println("Client disconnected");
  }
}