const int ledPin = 23;
const int trigPin = 2;
const int echoPin = 4;

bool ledState = false;
bool objectCloseLastCheck = false;

unsigned long lastToggleTime = 0;
const unsigned long toggleCooldown = 2000;

float getDistanceInches() {
  float total = 0;
  const int count = 3;
  
  for (int i = 0; i < count; i++) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH, 30000);
    if (duration == 0) {
      return -1;
    }

    float dist = (duration / 2.0) / 74.0;
    total += dist;
    delay(10);
  }
  
  return total / count;
}

void setup() {
  pinMode(ledPin, OUTPUT);
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  Serial.begin(115200);
  Serial.println("Enter 'on' or 'off' to control the LED manually.");
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim();

    if (input.equalsIgnoreCase("on")) {
      ledState = true;
      Serial.println("LED turned ON via serial");
    }
    else if (input.equalsIgnoreCase("off")) {
      ledState = false;
      Serial.println("LED turned OFF via serial");
    }
    else {
      Serial.println("Invalid input. Type 'on' or 'off'.");
    }
  }

  float distanceInches = getDistanceInches();

  bool objectClose = (distanceInches >= 1.0 && distanceInches <= 3.0);

  unsigned long currentMillis = millis();

  if (objectClose && !objectCloseLastCheck) {
    if (currentMillis - lastToggleTime >= toggleCooldown) {
      if (!ledState) {
        ledState = true;
        Serial.print("Object detected at ");
        Serial.print(distanceInches, 2);
        Serial.println(" inches — LED turned ON");
      } else {
        ledState = false;
        Serial.print("Object detected at ");
        Serial.print(distanceInches, 2);
        Serial.println(" inches — LED turned OFF");
      }
      lastToggleTime = currentMillis;
    }
  }

  objectCloseLastCheck = objectClose;

  digitalWrite(ledPin, ledState ? HIGH : LOW);

  delay(100);
}
