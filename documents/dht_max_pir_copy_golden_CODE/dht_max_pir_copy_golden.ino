#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "sec1.h" // Corrected include
#include <DHT.h>
#include <Wire.h>
#include "MAX30100_PulseOximeter.h"

#define DHTPIN 14      // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11    // DHT 11
#define TRIGGER_PIN 13 // Ultrasonic sensor trigger pin
#define ECHO_PIN 12   // Ultrasonic sensor echo pin

DHT dht(DHTPIN, DHTTYPE);

float h;
float t;
unsigned long lastMillis = 0;
unsigned long previousMillis = 0;
const long interval = 5000;

#define AWS_IOT_PUBLISH_TOPIC   "esp_4/pub"
#define AWS_IOT_SUBSCRIBE_TOPIC "esp_4/sub"

WiFiClientSecure net;

BearSSL::X509List cert(cacert);
BearSSL::X509List client_crt(client_cert);
BearSSL::PrivateKey key(privkey);

PubSubClient client(net);

time_t now;
time_t nowish = 1510592825;

#define REPORTING_PERIOD_MS 1000
PulseOximeter pox;
uint32_t lastMillisPulse = 0;

void onBeatDetected() {
    Serial.println("♥ Beat detected!");
}

void NTPConnect(void) {
  Serial.print("Setting time using SNTP");
  configTime(TIME_ZONE * 3600, 0 * 3600, "pool.ntp.org", "time.nist.gov");
  now = time(nullptr);
  while (now < nowish) {
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("done!");
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  Serial.print("Current time: ");
  Serial.print(asctime(&timeinfo));
}

void messageReceived(char *topic, byte *payload, unsigned int length) {
  Serial.print("Received [");
  Serial.print(topic);
  Serial.print("]: ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

void connectAWS() {
  delay(3000);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println(String("Attempting to connect to SSID: ") + String(WIFI_SSID));

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(1000);
  }

  NTPConnect();

  net.setTrustAnchors(&cert);
  net.setClientRSACert(&client_crt, &key);

  client.setServer(MQTT_HOST, 8883);
  client.setCallback(messageReceived);

  Serial.println("Connecting to AWS IOT");

  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(1000);
  }

  if (!client.connected()) {
    Serial.println("AWS IoT Timeout!");
    return;
  }
  client.subscribe(AWS_IOT_SUBSCRIBE_TOPIC);
  Serial.println("AWS IoT Connected!");
}

void publishMessage() {
  DynamicJsonDocument doc(512);
  doc["temperature"] = t;
  doc["humidity"] = h;
  doc["heartRate"] = pox.getHeartRate();
  doc["spo2"] = pox.getSpO2();

  long duration, distance;
  digitalWrite(TRIGGER_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIGGER_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIGGER_PIN, LOW);
  duration = pulseIn(ECHO_PIN, HIGH);
  distance = (duration * 0.034 / 2);
  doc["distance"] = distance;

  String alertMessage;
  if (distance <= 2) {
    alertMessage = "serine is full";
  } else if (distance <= 15) {
    alertMessage = "serine level is normal";
  } else {
    alertMessage = "serine is about to empty";
  }
  doc["alert"] = alertMessage;

  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  client.publish(AWS_IOT_PUBLISH_TOPIC, jsonBuffer);

  Serial.print(jsonBuffer);
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  connectAWS();
  dht.begin();
  pinMode(TRIGGER_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  Wire.begin();
  Serial.println("Initializing MAX30100...");
  
  int attempts = 0;
  while (!pox.begin()) {
      Serial.println("FAILED to initialize MAX30100! Retrying...");
      delay(2000);
      attempts++;
      if (attempts >= 5) {
          Serial.println("Max retries reached. Check wiring!");
          while (1);
      }
  }
  
  Serial.println("MAX30100 initialized.");
  pox.setIRLedCurrent(MAX30100_LED_CURR_50MA);
  pox.setOnBeatDetectedCallback(onBeatDetected);
}

void loop() {
  h = dht.readHumidity();
  t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println(F("Failed to read from DHT sensor!"));
    return;
  }

  now = time(nullptr);

  if (!client.connected()) {
    connectAWS();
  } else {
    client.loop();
    pox.update();
    if (millis() - lastMillis > 5000) {
      lastMillis = millis();
      publishMessage();
    }
  }
}
