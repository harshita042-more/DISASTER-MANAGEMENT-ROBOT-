
#define BLYNK_TEMPLATE_ID   "YOUR_TEMPLATE_ID"
#define BLYNK_TEMPLATE_NAME "Disaster Management Robot"
#define BLYNK_AUTH_TOKEN    "YOUR_AUTH_TOKEN"

#define BLYNK_PRINT Serial

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <TinyGPSPlus.h>
#include <ESP32Servo.h>

// -------- Wi-Fi --------
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

// -------- Pin configuration --------
#define TRIG_PIN       5
#define ECHO_PIN       18
#define SERVO_PIN      19

#define MQ2_PIN        34

#define FLAME1_PIN     25
#define FLAME2_PIN     26
#define FLAME3_PIN     27

#define GPS_RX_PIN     16  // ESP32 receives from GPS TX
#define GPS_TX_PIN     17  // ESP32 transmits to GPS RX

// -------- Settings to calibrate --------
const float HAND_DISTANCE_CM = 12.0;
const int MQ2_THRESHOLD = 1800; // Adjust after testing
const int SERVO_CLOSED = 0;
const int SERVO_OPEN = 90;

// Most flame sensor modules have active-LOW digital output.
const int FLAME_ACTIVE = LOW;

// -------- Objects --------
BlynkTimer timer;
TinyGPSPlus gps;
HardwareSerial GPSSerial(2);
Servo sanitizerServo;

// -------- State --------
bool handPresent = false;
bool dispensing = false;

unsigned long dispenseStarted = 0;
unsigned long lastAlertTime = 0;

const unsigned long DISPENSE_MS = 800;
const unsigned long ALERT_COOLDOWN_MS = 30000;

// -------- HC-SR04 distance --------
float getDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(3);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration =
      pulseIn(ECHO_PIN, HIGH, 30000);

  if (duration == 0) return -1;

  return duration * 0.0343 / 2.0;
}

// -------- Send alert to Blynk --------
void sendAlert(String message) {
  Serial.println("ALERT: " + message);

  if (Blynk.connected()) {
    Blynk.logEvent("safety_alert", message);
  }
}

// -------- Read GPS continuously --------
void readGPS() {
  while (GPSSerial.available()) {
    gps.encode(GPSSerial.read());
  }
}

// -------- Update sanitizer dispenser --------
void checkSanitizer() {
  float distance = getDistanceCM();

  if (distance > 0) {
    Blynk.virtualWrite(V0, distance);
  }

  // Trigger only once while a hand remains in front.
  if (distance > 0 &&
      distance <= HAND_DISTANCE_CM &&
      !handPresent && !dispensing) {

    handPresent = true;
    dispensing = true;
    dispenseStarted = millis();

    sanitizerServo.write(SERVO_OPEN);

    Blynk.virtualWrite(V1, "Dispensing sanitizer");
    Serial.println("Hand detected: dispensing sanitizer");
  }

  // Re-arm when the hand is removed.
  if (distance < 0 || distance > HAND_DISTANCE_CM) {
    handPresent = false;
  }

  // Close the servo after the dispensing interval.
  if (dispensing &&
      millis() - dispenseStarted >= DISPENSE_MS) {

    sanitizerServo.write(SERVO_CLOSED);
    dispensing = false;

    Blynk.virtualWrite(V1, "Ready");
  }
}

// -------- Read smoke and flame sensors --------
void checkSafetySensors() {
  int smokeValue = analogRead(MQ2_PIN);

  int flame1 = digitalRead(FLAME1_PIN);
  int flame2 = digitalRead(FLAME2_PIN);
  int flame3 = digitalRead(FLAME3_PIN);

  bool smokeDetected = smokeValue >= MQ2_THRESHOLD;
  bool fireDetected =
      flame1 == FLAME_ACTIVE ||
      flame2 == FLAME_ACTIVE ||
      flame3 == FLAME_ACTIVE;

  Blynk.virtualWrite(V2, smokeValue);
  Blynk.virtualWrite(V3, fireDetected ? 1 : 0);

  String alert = "";

  if (smokeDetected) {
    alert += "Smoke/gas threshold exceeded. ";
  }

  if (flame1 == FLAME_ACTIVE) alert += "Flame sensor 1 triggered. ";
  if (flame2 == FLAME_ACTIVE) alert += "Flame sensor 2 triggered. ";
  if (flame3 == FLAME_ACTIVE) alert += "Flame sensor 3 triggered. ";

  // Limit repeated notifications.
  if (alert.length() > 0 &&
      millis() - lastAlertTime >= ALERT_COOLDOWN_MS) {

    sendAlert(alert);
    lastAlertTime = millis();
  }
}

// -------- GPS location update every minute --------
void updateLocation() {
  if (gps.location.isValid() &&
      gps.location.age() < 10000) {

    double latitude = gps.location.lat();
    double longitude = gps.location.lng();

    Serial.printf("Latitude: %.6f\n", latitude);
    Serial.printf("Longitude: %.6f\n", longitude);

    Blynk.virtualWrite(V5, latitude);
    Blynk.virtualWrite(V6, longitude);

    String mapLink =
        "https://maps.google.com/?q=" +
        String(latitude, 6) + "," +
        String(longitude, 6);

    Blynk.virtualWrite(V7, mapLink);

  } else {
    Serial.println("GPS fix unavailable; waiting for satellites.");
    Blynk.virtualWrite(V7, "Waiting for GPS fix");
  }
}

// -------- Setup --------
void setup() {
  Serial.begin(115200);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  pinMode(FLAME1_PIN, INPUT);
  pinMode(FLAME2_PIN, INPUT);
  pinMode(FLAME3_PIN, INPUT);

  analogReadResolution(12);

  sanitizerServo.setPeriodHertz(50);
  sanitizerServo.attach(SERVO_PIN, 500, 2400);
  sanitizerServo.write(SERVO_CLOSED);

  GPSSerial.begin(
      9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Blynk.begin(
      BLYNK_AUTH_TOKEN, ssid, pass);

  timer.setInterval(500L, checkSanitizer);
  timer.setInterval(1000L, checkSafetySensors);
  timer.setInterval(60000L, updateLocation);

  Serial.println("Disaster Management Robot started.");
}

// -------- Main loop --------
void loop() {
  Blynk.run();
  timer.run();
  readGPS();
}
