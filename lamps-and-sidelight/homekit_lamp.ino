#include <WiFi.h>
#include "HomeSpan.h"

const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

WiFiServer server(80);
const int ledPin = 23;
bool ledState = false;

struct LEDAccessory : Service::LightBulb {
  SpanCharacteristic *power;

  LEDAccessory(int pin) {
    power = new Characteristic::On();
    pinMode(pin, OUTPUT);
  }

  boolean update() override {
    ledState = power->getNewVal();
    digitalWrite(ledPin, ledState ? HIGH : LOW);
    return true;
  }
};

void setup() {
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: ");
  Serial.println(WiFi.localIP());

  server.begin();

  homeSpan.setPairingCode("11122333");
  homeSpan.begin(Category::Lighting, "NIKHIL's Lights");

  new SpanAccessory();
    new Service::AccessoryInformation();
      new Characteristic::Identify();
      new Characteristic::Name("LED Light");
    new LEDAccessory(ledPin);
}

void loop() {
  homeSpan.poll();

  WiFiClient client = server.available();
  if (client) {
    String request = client.readStringUntil('\r');
    client.read();

    if (request.indexOf("led=on") >= 0) {
      ledState = true;
    }
    if (request.indexOf("led=off") >= 0) {
      ledState = false;
    }

    digitalWrite(ledPin, ledState ? HIGH : LOW);

    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println("Connection: close");
    client.println();

    client.println("<!DOCTYPE html><html>");
    client.println("<head><title>ESP32 LED Control</title>");
    client.println("<style>");
    client.println("body { font-family: Arial; text-align: center; margin-top: 50px; background-color: #f0f0f0; }");
    client.println(".button { padding: 15px 25px; font-size: 24px; cursor: pointer; color: #fff; border: none; border-radius: 5px; margin: 10px; }");
    client.println(".on { background-color: #28a745; }");
    client.println(".off { background-color: #dc3545; }");
    client.println("</style></head><body>");

    client.println("<h1>ESP32 LED Control</h1>");
    client.print("<p>LED is currently <strong>");
    client.print(ledState ? "ON" : "OFF");
    client.println("</strong></p>");

    client.println("<a href=\"/?led=on\"><button class=\"button on\">Turn LED ON</button></a>");
    client.println("<a href=\"/?led=off\"><button class=\"button off\">Turn LED OFF</button></a>");

    client.println("</body></html>");
    client.println();

    client.stop();
  }
}