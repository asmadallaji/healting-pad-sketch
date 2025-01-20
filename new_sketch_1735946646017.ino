#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <ArduinoJson.h>

#define WIFI_SSID "Orange-C7A8"
#define WIFI_PASSWORD "5HJEHF53GJ5"
#define API_KEY "AIzaSyDI0SAlMvlNfSop56ClEEfBvNOcZTzLGHE"
#define FIRESTORE_HOST "firestore.googleapis.com"
#define PROJECT_ID "heating-pad" 
// Firebase paths
const String CONFIG_PATH = "configuration/default";
const String TEMP_PATH = "temperature_readings";
const String NOTIFICATION_PATH = "notifications";


// Firebase objects
FirebaseConfig config;
FirebaseAuth auth;
FirebaseData fbdo;

#define PIR_PIN 15
#define LED_PIN 2

unsigned long lastTemperatureChange = 0;
const int decreaseInterval = 5000;

bool allowNotifications = false;
bool enableSensors = true;
int temperature = 0;
float temperatureMin = 18.0;
float temperatureMax = 28.0;
unsigned long lastConfigCheck = 0;
const unsigned long configCheckInterval = 10000;
int motionDetected = 0;
bool notificationTempMinSent = false;
bool notificationTempMaxSent = false;

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
  json.set("fields/timestamp/integerValue", millis());

  if (Firebase.Firestore.createDocument(&fbdo, PROJECT_ID, "", TEMP_PATH.c_str(), json.raw())) {
    Serial.println("Temperature updated: " + String(temperature));
  } else {
    Serial.println("Error updating temperature: " + fbdo.errorReason());
  }
}

void transformMotionToTemperature() {
  
  if (!enableSensors) {
    if ((millis() - lastTemperatureChange > decreaseInterval)) {
      changeTemperature(temperature-1);
    Serial.print("No motion detected. Temperature decreasing: " + String(temperature));
    }
    return;
  }
  
  int motionDetected = digitalRead(PIR_PIN);
  if (motionDetected > 0) {
    Serial.println("Motion detected!");
    changeTemperature(temperature+1);
  }
}
void changeTemperature(int newTemperature) {
   lastTemperatureChange = millis();
    temperature = newTemperature;
    updateTemperature(temperature);
    toggleLed();
    handleNotifications();
}
void sendNotification(String message) {
  FirebaseJson json;
  json.set("fields/message/stringValue", message);
  json.set("fields/timestamp/integerValue", millis());
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
    } else if (temperature <= temperatureMin && !notificationTempMinSent) {
      Serial.println("Low temperature alert! Temperature: " + String(temperature));
      sendNotification("Low temperature alert! Temperature: " + String(temperature));
      notificationTempMaxSent = false;
      notificationTempMinSent = true;
    }
}

void toggleLed() {
  if(temperature < temperatureMax && temperature > temperatureMin) {
     digitalWrite(LED_PIN, HIGH);
    Serial.println("Temperature " + String(temperature) + "°C is above minimum temperature! LED ON.");
  } else {
    digitalWrite(LED_PIN, LOW);
    Serial.println("Temperature " + String(temperature) + "°C is below minimum temperature! LED OFF.");
  }
}

void setup() {
  temperature = random(0,40);
  Serial.begin(115200);
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  connectToWiFi();
  initializeFirebase();
  fetchConfiguration();
}

void loop() {
  if (millis() - lastConfigCheck > configCheckInterval) {
    fetchConfiguration();
    lastConfigCheck = millis();
  }
  transformMotionToTemperature();
  
  delay(5000);
}