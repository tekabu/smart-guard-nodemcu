#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <MFRC522v2.h>
#include <MFRC522DriverSPI.h>
#include <MFRC522DriverPinSimple.h>
#include <MFRC522Debug.h>

bool SERIAL_DEBUG = false;

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
const char* mqtt_lock_open_topic = "smartguard/lock/open";
const char* mqtt_verify_card_topic = "smartguard/verify/card";
const char* mqtt_verify_fingerprint_topic = "smartguard/verify/fingerprint";

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

void Serial_print(String message, bool ln = false) {
  if (SERIAL_DEBUG) {
    if (ln) {
      Serial.println(message);
    } else {
      Serial.print(message);
    }
  }
}

void setup_wifi() {
  delay(10);
  Serial_print("", true);
  Serial_print("Connecting to ");
  Serial_print(ssid, true);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial_print(".");
  }

  Serial_print("", true);
  Serial_print("WiFi connected", true);
  Serial_print("IP address: ", true);
  if (SERIAL_DEBUG) {
    Serial.println(WiFi.localIP());
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial_print("Message arrived [");
  Serial_print(String(topic));
  Serial_print("] ");

  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial_print(message, true);

  if (String(topic) == mqtt_register_topic) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      String errorMsg = "JSON parse error: ";
      errorMsg += error.c_str();
      Serial_print(errorMsg, true);
      sendCardRegistrationError(errorMsg);
    } else if (!doc.containsKey("reference")) {
      String errorMsg = "Missing 'reference' key in JSON payload";
      Serial_print(errorMsg, true);
      sendCardRegistrationError(errorMsg);
    } else {
      registrationReference = doc["reference"].as<String>();
      registrationMode = true;
      Serial_print("Registration mode activated!", true);
      Serial_print("Reference: ");
      Serial_print(registrationReference, true);
      Serial_print("Waiting for card scan...", true);
    }
  } else if (String(topic) == mqtt_register_fingerprint_topic) {
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, message);

    if (error) {
      String errorMsg = "JSON parse error: ";
      errorMsg += error.c_str();
      Serial_print(errorMsg, true);
      sendFingerprintRegistrationError(errorMsg);
    } else if (!doc.containsKey("reference")) {
      String errorMsg = "Missing 'reference' key in JSON payload";
      Serial_print(errorMsg, true);
      sendFingerprintRegistrationError(errorMsg);
    } else {
      fingerprintRegistrationReference = doc["reference"].as<String>();
      fingerprintRegistrationMode = true;
      Serial_print("Fingerprint registration mode activated!", true);
      Serial_print("Reference: ");
      Serial_print(fingerprintRegistrationReference, true);
      Serial.print("$FP_REG#");
    }
  } else if (String(topic) == mqtt_lock_open_topic) {
    Serial.print("$OPEN_LOCK#");
    Serial_print("Lock open command sent", true);
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial_print("Attempting MQTT connection...");
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str())) {
      Serial_print("connected", true);
      client.subscribe(mqtt_register_topic);
      Serial_print("Subscribed to: ");
      Serial_print(mqtt_register_topic, true);
      client.subscribe(mqtt_register_fingerprint_topic);
      Serial_print("Subscribed to: ");
      Serial_print(mqtt_register_fingerprint_topic, true);
      client.subscribe(mqtt_lock_open_topic);
      Serial_print("Subscribed to: ");
      Serial_print(mqtt_lock_open_topic, true);
    } else {
      Serial_print("failed, rc=");
      Serial_print(String(client.state()));
      Serial_print(" try again in 5 seconds", true);
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
  Serial_print("Published: ");
  Serial_print(jsonBuffer, true);
}

void sendCardVerification(int reader, String cardId) {
  StaticJsonDocument<200> doc;
  doc["card_reader"] = reader;
  doc["card_id"] = cardId;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_verify_card_topic, jsonBuffer);
  Serial_print("Card Verification Published: ");
  Serial_print(jsonBuffer, true);
}

void sendCardRegistrationSuccess(String reference, String cardId) {
  StaticJsonDocument<200> doc;
  doc["reference"] = reference;
  doc["card_id"] = cardId;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_success_topic, jsonBuffer);
  Serial_print("Card Registration Success Published: ");
  Serial_print(jsonBuffer, true);
}

void sendFingerprintRegistrationSuccess(String reference, String fingerprintId) {
  StaticJsonDocument<200> doc;
  doc["reference"] = reference;
  doc["fingerprint_id"] = fingerprintId;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_fingerprint_success_topic, jsonBuffer);
  Serial_print("Fingerprint Registration Success Published: ");
  Serial_print(jsonBuffer, true);
}

void sendCardRegistrationError(String errorMessage) {
  StaticJsonDocument<200> doc;
  doc["error"] = errorMessage;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_error_topic, jsonBuffer);
  Serial_print("Card Registration Error Published: ");
  Serial_print(jsonBuffer, true);
}

void sendFingerprintRegistrationError(String errorMessage) {
  StaticJsonDocument<200> doc;
  doc["error"] = errorMessage;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_register_fingerprint_error_topic, jsonBuffer);
  Serial_print("Fingerprint Registration Error Published: ");
  Serial_print(jsonBuffer, true);
}

void sendFingerprintVerification(int reader, int fingerprintId) {
  StaticJsonDocument<200> doc;
  doc["fingerprint_reader"] = reader;
  doc["fingerprint_id"] = fingerprintId;

  char jsonBuffer[200];
  serializeJson(doc, jsonBuffer);

  client.publish(mqtt_verify_fingerprint_topic, jsonBuffer);
  Serial_print("Fingerprint Verification Published: ");
  Serial_print(jsonBuffer, true);
}

void setup() {
  Serial.begin(115200);
  while (!Serial);

  mfrc522_1.PCD_Init();
  mfrc522_2.PCD_Init();

  if (SERIAL_DEBUG) {
    MFRC522Debug::PCD_DumpVersionToSerial(mfrc522_1, Serial);
    MFRC522Debug::PCD_DumpVersionToSerial(mfrc522_2, Serial);
  }

  Serial_print("Scan PICC to see UID, SAK, type, and data blocks...", true);

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

    Serial.print("[DEBUG] Char received: '");
    Serial.print(c);
    Serial.print("' (ASCII: ");
    Serial.print((int)c);
    Serial.print(") Buffer: '");
    Serial.print(serialBuffer);
    Serial.println("'");

    if (c == '\n' || c == '\r') {
      if (serialBuffer == "DEBUGON") {
        SERIAL_DEBUG = true;
        Serial.println("Debug mode enabled");
        serialBuffer = "";
        continue;
      } else if (serialBuffer == "DEBUGOFF") {
        SERIAL_DEBUG = false;
        Serial.println("Debug mode disabled");
        serialBuffer = "";
        continue;
      }
    }

    if (c == '$') {
      serialBuffer = "$";
      Serial_print("[DEBUG] Starting new message buffer", true);
    } else if (c == '#') {
      Serial_print("[DEBUG] End delimiter found. Buffer content: '");
      Serial_print(serialBuffer);
      Serial_print("'", true);

      if (serialBuffer.startsWith("$FP_OK: ")) {
        int idStart = 8;
        String fingerprintId = serialBuffer.substring(idStart);

        Serial_print("\nFingerprint ID received: ");
        Serial_print(fingerprintId, true);

        if (fingerprintRegistrationMode) {
          sendFingerprintRegistrationSuccess(fingerprintRegistrationReference, fingerprintId);
          fingerprintRegistrationMode = false;
          fingerprintRegistrationReference = "";
          Serial_print("Fingerprint registration complete! Mode deactivated.", true);
        }
      } else if (serialBuffer.startsWith("$FP_ID: ")) {
        int idStart = 8;
        String data = serialBuffer.substring(idStart);
        int commaIndex = data.indexOf(',');

        if (commaIndex > 0) {
          int readerIndex = data.substring(0, commaIndex).toInt();
          int templateId = data.substring(commaIndex + 1).toInt();

          Serial_print("\nFingerprint verification - Reader: ");
          Serial_print(String(readerIndex));
          Serial_print(" Template: ");
          Serial_print(String(templateId), true);

          if (!fingerprintRegistrationMode) {
            sendFingerprintVerification(readerIndex, templateId);
          }
        } else {
          Serial_print("[DEBUG] Invalid $FP_ID format", true);
        }
      } else {
        Serial_print("[DEBUG] Buffer does not match expected format", true);
      }
      serialBuffer = "";
    } else if (serialBuffer.startsWith("$")) {
      serialBuffer += c;
      Serial_print("[DEBUG] Added char to buffer (message)", true);
    } else if (c != '\n' && c != '\r') {
      serialBuffer += c;
      Serial_print("[DEBUG] Added char to buffer (command)", true);
    }
  }

  if (mfrc522_1.PICC_IsNewCardPresent()) {
    if (mfrc522_1.PICC_ReadCardSerial()) {
      String cardId = uidToString(mfrc522_1.uid);
      Serial_print("Card ID1: ");
      Serial_print(cardId, true);

      if (registrationMode) {
        sendCardRegistrationSuccess(registrationReference, cardId);
        registrationMode = false;
        registrationReference = "";
        Serial_print("Registration complete! Mode deactivated.", true);
      } else {
        sendCardVerification(1, cardId);
      }

      mfrc522_1.PICC_HaltA();
      mfrc522_1.PCD_StopCrypto1();
      delay(1000);
    }
  }

  if (mfrc522_2.PICC_IsNewCardPresent()) {
    if (mfrc522_2.PICC_ReadCardSerial()) {
      String cardId = uidToString(mfrc522_2.uid);
      Serial_print("Card ID2: ");
      Serial_print(cardId, true);

      if (registrationMode) {
        sendCardRegistrationSuccess(registrationReference, cardId);
        registrationMode = false;
        registrationReference = "";
        Serial_print("Registration complete! Mode deactivated.", true);
      } else {
        sendCardVerification(2, cardId);
      }

      mfrc522_2.PICC_HaltA();
      mfrc522_2.PCD_StopCrypto1();
      delay(1000);
    }
  }
}
