#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>
#include <RTClib.h>
#include <DHT.h>

#define WIFI_SSID "Galaxy M52 5G5A4F"
#define WIFI_PASSWORD "hjvg3848"
#define API_KEY "AIzaSyDI0SAlMvlNfSop56ClEEfBvNOcZTzLGHE"
#define FIRESTORE_HOST "firestore.googleapis.com"
#define PROJECT_ID "heating-pad"
// Firebase paths
const String CONFIG_PATH = "configuration/default";
const String TEMP_PATH = "temperature_readings";
const String NOTIFICATION_PATH = "notifications";

// Capteur DHT
#define DHTPIN 4      
#define DHTTYPE DHT22  
DHT dht(DHTPIN, DHTTYPE);

FirebaseConfig config;
FirebaseAuth auth;
FirebaseData fbdo;

#define PIR_PIN 15
#define LED_PIN 2

unsigned long lastTemperatureChange = 0;
const int decreaseInterval = 5000;

bool allowNotifications = false;
bool enableSensors = true;
float temperature = 0;
float temperatureMin = 18.0;
float temperatureMax = 28.0;
unsigned long lastConfigCheck = 0;
const unsigned long configCheckInterval = 10000;
int motionDetected = 0;
bool notificationTempMinSent = false;
bool notificationTempMaxSent = false;
RTC_DS3231 rtc;


void connectToWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to Wi-Fi...");
  }
  Serial.println("Connected to Wi-Fi!");
}

void initializeFirebase() {
  config.api_key = API_KEY;
  config.host = FIRESTORE_HOST;
 
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase signup successful");
  } else {
    Serial.printf("Firebase signup failed: %s\n", config.signer.signupError.message.c_str());
  }
 
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

/*
  read configuration from firestore configuration collection
  */
void fetchConfiguration() {
  if (Firebase.Firestore.getDocument(&fbdo, PROJECT_ID, "", CONFIG_PATH.c_str())) {
    Serial.println("Configuration fetched successfully!");

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, fbdo.payload());
    Serial.println(fbdo.payload());
   
    //read allowNotifications from firestore
    if (doc["fields"]["allowNotifications"]["booleanValue"].is<bool>()) {
      allowNotifications = doc["fields"]["allowNotifications"]["booleanValue"];
    }
    //read enableSensors from firestore
    if (doc["fields"]["enableSensors"]["booleanValue"].is<bool>()) {
      enableSensors = doc["fields"]["enableSensors"]["booleanValue"];
    }
    //read temperatureMin from firestore
    if (doc["fields"]["temperatureMin"]["doubleValue"].is<const char*>()) {
      temperatureMin = String(doc["fields"]["temperatureMin"]["doubleValue"].as<const char*>()).toFloat();
    } else if (doc["fields"]["temperatureMin"]["integerValue"].is<const char*>()) {
      temperatureMin = String(doc["fields"]["temperatureMin"]["integerValue"].as<const char*>()).toFloat();
    } else {
      Serial.println("temperatureMin not found or invalid type!");
    }
    //read temperatureMax from firestore
    if (doc["fields"]["temperatureMax"]["doubleValue"].is<const char*>()) {
      temperatureMax = String(doc["fields"]["temperatureMax"]["doubleValue"].as<const char*>()).toFloat();
    } else if (doc["fields"]["temperatureMax"]["integerValue"].is<const char*>()) {
      temperatureMax = String(doc["fields"]["temperatureMax"]["integerValue"].as<const char*>()).toFloat();
    } else {
      Serial.println("temperatureMax not found or invalid type!");
    }

    Serial.println("allowNotifications: " + String(allowNotifications));
    Serial.println("enableSensors: " + String(enableSensors));
    Serial.println("temperatureMin: " + String(temperatureMin));
    Serial.println("temperatureMax: " + String(temperatureMax));
  } else {
    Serial.println("Error fetching configuration: " + fbdo.errorReason());
  }
}

/*
  save realtime temperature in firestore temperature_readings collection
  */
void updateTemperature(float temperature) {
  FirebaseJson json;
  json.set("fields/temperature/doubleValue", temperature);
  json.set("fields/timestamp/integerValue", rtc.now().unixtime());

  if (Firebase.Firestore.createDocument(&fbdo, PROJECT_ID, "", TEMP_PATH.c_str(), json.raw())) {
    Serial.println("Temperature updated: " + String(temperature));
  } else {
    Serial.println("Error updating temperature: " + fbdo.errorReason());
  }
}

void transformMotionToTemperature() {
  if (!enableSensors) {
    if ((millis() - lastTemperatureChange > decreaseInterval)) {
      Serial.print("No motion detected. Temperature decreasing..");
      changeTemperature(temperature - 1);
      toggleLed();
    }
    return;
  }
 
  int motionDetected = digitalRead(PIR_PIN);
  if (motionDetected > 0) {
     Serial.println("Motion detected!");
     readDHTSensor();
  }
}

void changeTemperature(int newTemperature) {
    lastTemperatureChange = millis();
    temperature = newTemperature;
     if (temperature > temperatureMax) {
         temperature = temperatureMax;
     }
    //update temperature in firebase
    updateTemperature(temperature);
    toggleLed();
    handleNotifications();
}

void sendNotification(String message) {
  FirebaseJson json;
  json.set("fields/message/stringValue", message);
  json.set("fields/timestamp/integerValue", rtc.now().unixtime());
  json.set("fields/read/booleanValue", false);

  if (Firebase.Firestore.createDocument(&fbdo, PROJECT_ID, "", NOTIFICATION_PATH.c_str(), json.raw())) {
    Serial.println("notification send: " + String(temperature));
  } else {
    Serial.println("Error sending notification: " + fbdo.errorReason());
  }
}

void handleNotifications() {
  if (!allowNotifications) return;
  if (temperature >= temperatureMax && !notificationTempMaxSent) {
      Serial.println("High temperature alert! Temperature: " + String(temperature));
      sendNotification("High temperature alert! Temperature: " + String(temperature));
      notificationTempMaxSent = true;
      notificationTempMinSent = false;
      temperature = temperatureMax;
    } else if (temperature <= temperatureMin && !notificationTempMinSent) {
      Serial.println("Low temperature alert! Temperature: " + String(temperature));
      sendNotification("Low temperature alert! Temperature: " + String(temperature));
      notificationTempMaxSent = false;
      notificationTempMinSent = true;
    }
}

void toggleLed() {
  if (temperature >= temperatureMin && temperature <= temperatureMax) {
    digitalWrite(LED_PIN, HIGH);
    Serial.println("LED Temperature " + String(temperature) + "°C is within the range. LED ON.");
  } else {
    digitalWrite(LED_PIN, LOW);
    Serial.println("LED Temperature " + String(temperature) + "°C is outside the range. LED OFF.");
  }
}

void readDHTSensor() {
  if (!enableSensors)  return;
  temperature = dht.readTemperature();
  if (isnan(temperature)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  changeTemperature(temperature);
}

void setup() {
  temperature = random(0,40);
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  connectToWiFi();
  initializeFirebase();
  fetchConfiguration();
  dht.begin();
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
  }
}

void loop() {
  if (millis() - lastConfigCheck > configCheckInterval) {
    fetchConfiguration();
    lastConfigCheck = millis();
  }
 
  transformMotionToTemperature();
 
  delay(5000);
}