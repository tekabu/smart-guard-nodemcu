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

bool registrationMode = false;
String registrationReference = "";

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

    if (!error) {
      if (doc.containsKey("reference")) {
        registrationReference = doc["reference"].as<String>();
        registrationMode = true;
        Serial.println("Registration mode activated!");
        Serial.print("Reference: ");
        Serial.println(registrationReference);
        Serial.println("Waiting for card scan...");
      }
    } else {
      Serial.print("JSON parse error: ");
      Serial.println(error.c_str());
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

void sendRegistrationSuccess(String reference, String cardId) {
  StaticJsonDocument<200> doc;
  doc["reference"] = reference;
  doc["card_id"] = cardId;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_success_topic, jsonBuffer);
  Serial.print("Registration Success Published: ");
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

  if (mfrc522_1.PICC_IsNewCardPresent()) {
    if (mfrc522_1.PICC_ReadCardSerial()) {
      String cardId = uidToString(mfrc522_1.uid);
      Serial.print("Card ID1: ");
      Serial.println(cardId);

      if (registrationMode) {
        sendRegistrationSuccess(registrationReference, cardId);
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
        sendRegistrationSuccess(registrationReference, cardId);
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
