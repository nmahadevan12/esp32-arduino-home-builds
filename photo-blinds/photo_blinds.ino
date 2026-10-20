const int IN1 = 8;
const int IN2 = 11;
const int ENA = 9;
const int sensorPin = A0;
const int buttonPin = 2; 
const int ledPin = 3;    

enum Direction { IDLE,
                 FORWARD,
                 BACKWARD };
Direction lastDirection = BACKWARD;  

enum Mode { AUTO,
            MANUAL };
Mode currentMode = AUTO;

unsigned long lastPhotoCheck = 0;
const unsigned long photoInterval = 1000;

int aboveThresholdCount = 0;
int belowThresholdCount = 0;
const int threshold = 250;
const int requiredCount = 30;

unsigned long lastButtonCheck = 0;
const unsigned long debounceDelay = 200;
bool lastButtonState = HIGH;

bool modeJustReturnedFromManual = false;

void setup() {
  Serial.begin(9600);

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);  
  pinMode(ledPin, OUTPUT);      

  digitalWrite(ledPin, LOW);
  stopMotor();

  Serial.println("Startup complete. Blinds assumed OPEN.");
}

void loop() {
  checkButton();

  if (currentMode == AUTO) {
    digitalWrite(ledPin, LOW);
    checkLightLevel();
  } else {
    digitalWrite(ledPin, HIGH); 
    Serial.println("Manual Mode - No automatic adjustments.");
    delay(1000);  
  }
}

void checkButton() {
  unsigned long currentTime = millis();
  if (currentTime - lastButtonCheck > debounceDelay) {
    bool buttonState = digitalRead(buttonPin);
    if (buttonState == LOW && lastButtonState == HIGH) {
      if (currentMode == AUTO) {
        currentMode = MANUAL;
        Serial.println("Mode changed to: MANUAL");
      } else {
        currentMode = AUTO;
        Serial.println("Mode changed to: AUTO");

        if (lastDirection == FORWARD) {
          lastDirection = BACKWARD;
          Serial.println("Assuming user manually opened blinds.");
        } else if (lastDirection == BACKWARD) {
          lastDirection = FORWARD;  
          Serial.println("Assuming user manually closed blinds.");
        }

        aboveThresholdCount = 0;
        belowThresholdCount = 0;

        modeJustReturnedFromManual = true;
      }
    }
    lastButtonState = buttonState;
    lastButtonCheck = currentTime;
  }
}

void checkLightLevel() {
  unsigned long currentTime = millis();
  if (currentTime - lastPhotoCheck >= photoInterval || modeJustReturnedFromManual) {
    lastPhotoCheck = currentTime;
    modeJustReturnedFromManual = false;

    int value = analogRead(sensorPin);
    Serial.print("Analog Value: ");
    Serial.println(value);

    if (value > threshold) {
      aboveThresholdCount++;
      belowThresholdCount = 0;
    } else {
      belowThresholdCount++;
      aboveThresholdCount = 0;
    }

    if (belowThresholdCount >= requiredCount && lastDirection != FORWARD) {
      Serial.println("AUTO: Closing blinds due to darkness.");
      spinMotorForward();
      delay(8000);
      stopMotor();
      lastDirection = FORWARD;

      aboveThresholdCount = 0;
      belowThresholdCount = 0;
    }

    if (aboveThresholdCount >= requiredCount && lastDirection != BACKWARD) {
      Serial.println("AUTO: Opening blinds due to brightness.");
      spinMotorReverse();
      delay(8000);
      stopMotor();
      lastDirection = BACKWARD;

      aboveThresholdCount = 0;
      belowThresholdCount = 0;
    }
  }
}

void spinMotorForward() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 255);
}

void spinMotorReverse() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  analogWrite(ENA, 255);
}

void stopMotor() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, LOW);
  analogWrite(ENA, 0);
}