#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

const char* ssid = "HUAWEI-hdVq";
const char* password = "DG2MJrzC";
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic = "smartguard/rfid";
const char* mqtt_register_topic = "smartguard/register/card";
const char* mqtt_register_success_topic = "smartguard/register/card/success";
const char* mqtt_register_error_topic = "smartguard/register/card/error";
const char* mqtt_register_fingerprint_topic = "smartguard/register/fingerprint";
const char* mqtt_register_fingerprint_success_topic = "smartguard/register/fingerprint/success";
const char* mqtt_register_fingerprint_error_topic = "smartguard/register/fingerprint/error";

bool registrationMode = false;
String registrationReference = "";
bool fingerprintRegistrationMode = false;
String fingerprintRegistrationReference = "";
String serialBuffer = "";

MFRC522DriverPinSimple ss_pin_1(D8);
MFRC522DriverPinSimple ss_pin_2(D1);

MFRC522DriverSPI driver_1{ ss_pin_1 };
MFRC522DriverSPI driver_2{ ss_pin_2 };

MFRC522 mfrc522_1{ driver_1 };
MFRC522 mfrc522_2{ driver_2 };

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  if (String(topic) == mqtt_register_topic) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      String errorMsg = "JSON parse error: ";
      errorMsg += error.c_str();
      Serial.println(errorMsg);
      sendCardRegistrationError(errorMsg);
    } else if (!doc.containsKey("reference")) {
      String errorMsg = "Missing 'reference' key in JSON payload";
      Serial.println(errorMsg);
      sendCardRegistrationError(errorMsg);
    } else {
      registrationReference = doc["reference"].as<String>();
      registrationMode = true;
      Serial.println("Registration mode activated!");
      Serial.print("Reference: ");
      Serial.println(registrationReference);
      Serial.println("Waiting for card scan...");
    }
  } else if (String(topic) == mqtt_register_fingerprint_topic) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      String errorMsg = "JSON parse error: ";
      errorMsg += error.c_str();
      Serial.println(errorMsg);
      sendFingerprintRegistrationError(errorMsg);
    } else if (!doc.containsKey("reference")) {
      String errorMsg = "Missing 'reference' key in JSON payload";
      Serial.println(errorMsg);
      sendFingerprintRegistrationError(errorMsg);
    } else {
      fingerprintRegistrationReference = doc["reference"].as<String>();
      fingerprintRegistrationMode = true;
      Serial.println("Fingerprint registration mode activated!");
      Serial.print("Reference: ");
      Serial.println(fingerprintRegistrationReference);
      Serial.print("$FP_REG#");
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(mqtt_register_topic);
      Serial.print("Subscribed to: ");
      Serial.println(mqtt_register_topic);
      client.subscribe(mqtt_register_fingerprint_topic);
      Serial.print("Subscribed to: ");
      Serial.println(mqtt_register_fingerprint_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

String uidToString(MFRC522::Uid &uid) {
  String cardId = "";
  for (byte i = 0; i < uid.size; i++) {
    if (uid.uidByte[i] < 0x10) {
      cardId += "0";
    }
    cardId += String(uid.uidByte[i], HEX);
  }
  cardId.toUpperCase();
  return cardId;
}

void sendCardData(int reader, String cardId) {
  StaticJsonDocument<200> doc;
  doc["card_reader"] = reader;
  doc["card_id"] = cardId;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_topic, jsonBuffer);
  Serial.print("Published: ");
  Serial.println(jsonBuffer);
}

void sendCardRegistrationSuccess(String reference, String cardId) {
  StaticJsonDocument<200> doc;
  doc["reference"] = reference;
  doc["card_id"] = cardId;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_success_topic, jsonBuffer);
  Serial.print("Card Registration Success Published: ");
  Serial.println(jsonBuffer);
}

void sendFingerprintRegistrationSuccess(String reference, String fingerprintId) {
  StaticJsonDocument<200> doc;
  doc["reference"] = reference;
  doc["fingerprint_id"] = fingerprintId;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_fingerprint_success_topic, jsonBuffer);
  Serial.print("Fingerprint Registration Success Published: ");
  Serial.println(jsonBuffer);
}

void sendCardRegistrationError(String errorMessage) {
  StaticJsonDocument<200> doc;
  doc["error"] = errorMessage;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_error_topic, jsonBuffer);
  Serial.print("Card Registration Error Published: ");
  Serial.println(jsonBuffer);
}

void sendFingerprintRegistrationError(String errorMessage) {
  StaticJsonDocument<200> doc;
  doc["error"] = errorMessage;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_fingerprint_error_topic, jsonBuffer);
  Serial.print("Fingerprint Registration Error Published: ");
  Serial.println(jsonBuffer);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  mfrc522_1.PCD_Init();
  mfrc522_2.PCD_Init();

  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522_1, Serial);
  MFRC522Debug::PCD_DumpVersionToSerial(mfrc522_2, Serial);

  Serial.println("Scan PICC to see UID, SAK, type, and data blocks...");

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '$') {
      serialBuffer = "$";
    } else if (c == '#') {
      if (serialBuffer.startsWith("$FP_OK: ")) {
        int idStart = 8;
        String fingerprintId = serialBuffer.substring(idStart);

        Serial.print("\nFingerprint ID received: ");
        Serial.println(fingerprintId);

        if (fingerprintRegistrationMode) {
          sendFingerprintRegistrationSuccess(fingerprintRegistrationReference, fingerprintId);
          fingerprintRegistrationMode = false;
          fingerprintRegistrationReference = "";
          Serial.println("Fingerprint registration complete! Mode deactivated.");
        }
      }
      serialBuffer = "";
    } else if (serialBuffer.length() > 0 || serialBuffer.startsWith("$")) {
      serialBuffer += c;
    }
  }

  if (mfrc522_1.PICC_IsNewCardPresent()) {
    if (mfrc522_1.PICC_ReadCardSerial()) {
      String cardId = uidToString(mfrc522_1.uid);
      Serial.print("Card ID1: ");
      Serial.println(cardId);

      if (registrationMode) {
        sendCardRegistrationSuccess(registrationReference, cardId);
        registrationMode = false;
        registrationReference = "";
        Serial.println("Registration complete! Mode deactivated.");
      } else {
        sendCardData(1, cardId);
      }

      mfrc522_1.PICC_HaltA();
      mfrc522_1.PCD_StopCrypto1();
      delay(1000);
    }
  }

  if (mfrc522_2.PICC_IsNewCardPresent()) {
    if (mfrc522_2.PICC_ReadCardSerial()) {
      String cardId = uidToString(mfrc522_2.uid);
      Serial.print("Card ID2: ");
      Serial.println(cardId);

      if (registrationMode) {
        sendCardRegistrationSuccess(registrationReference, cardId);
        registrationMode = false;
        registrationReference = "";
        Serial.println("Registration complete! Mode deactivated.");
      } else {
        sendCardData(2, cardId);
      }

      mfrc522_2.PICC_HaltA();
      mfrc522_2.PCD_StopCrypto1();
      delay(1000);
    }
  }
}
