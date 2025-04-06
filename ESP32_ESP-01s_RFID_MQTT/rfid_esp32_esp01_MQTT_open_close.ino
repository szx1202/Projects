/*
 * questo programma riconosce i token rfid e garantisce l'accesso
 * Per chiudere la "porta" aperta si deve utilizzare un altro token registato
 * invia il segnale di riconosciuto a un esp01-s che garantisce l'azione permessa 
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// WiFi and MQTT
const char* ssid = "LZ_24G";
const char* password = "*andromedA01.";
const char* mqtt_server = "192.168.1.104";

WiFiClient espClient;
PubSubClient client(espClient);

// RFID setup
#define SS_PIN  5
#define RST_PIN 4
MFRC522 rfid(SS_PIN, RST_PIN);

// LCD setup
LiquidCrystal_I2C lcd(0x27, 16, 2);

// LED pins
#define GREEN_LED 26
#define RED_LED   27

// Known UIDs
const int NUM_CARDS = 3;
String knownUIDs[NUM_CARDS] = {
  "1F8509C5",
  "ECB2F904",
  "DEADBEEF"
};

// MQTT topic
const char* mqtt_topic = "sensor/rfid";

// Track door state
bool doorOpen = false;

// ----- Functions -----

void setup_wifi() {
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected. IP: " + WiFi.localIP().toString());
}

void reconnect_mqtt() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP32_RFID_Client")) {
      Serial.println("connected.");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5s");
      delay(5000);
    }
  }
}

void sendMQTTMessage(const char* message) {
  if (!client.connected()) {
    reconnect_mqtt();
  }
  client.publish(mqtt_topic, message);
  Serial.print("MQTT -> ");
  Serial.print(mqtt_topic);
  Serial.print(": ");
  Serial.println(message);
}

bool isKnownUID(String uid) {
  for (int i = 0; i < NUM_CARDS; i++) {
    if (uid == knownUIDs[i]) {
      return true;
    }
  }
  return false;
}

// ----- Main Setup -----

void setup() {
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, 1883);

  SPI.begin();
  rfid.PCD_Init();

  Wire.begin(21, 22);
  lcd.begin();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Door CLOSED");

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
}

// ----- Main Loop -----

void loop() {
  client.loop();  // Keep MQTT alive

  if (!rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
    delay(100);
    return;
  }

  // Read UID
  String uidStr = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    if (rfid.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(rfid.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();
  Serial.println("Scanned UID: " + uidStr);

  // Halt PICC
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("UID:");
  lcd.setCursor(0, 1);
  lcd.print(uidStr);
  delay(1000);

  if (isKnownUID(uidStr)) {
    lcd.clear();
    digitalWrite(RED_LED, LOW);

    if (!doorOpen) {
      lcd.setCursor(0, 0);
      lcd.print("Access Granted");
      lcd.setCursor(0, 1);
      lcd.print("Door OPEN");
      Serial.println("Door OPENED");
      digitalWrite(GREEN_LED, HIGH);
      sendMQTTMessage("1");
      doorOpen = true;
    } else {
      lcd.setCursor(0, 0);
      lcd.print("Access Granted");
      lcd.setCursor(0, 1);
      lcd.print("Door CLOSED");
      Serial.println("Door CLOSED");
      digitalWrite(GREEN_LED, HIGH);
      sendMQTTMessage("2");
      doorOpen = false;
    }
  } else {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Access Denied");
    Serial.println("Unknown card.");
    digitalWrite(GREEN_LED, LOW);
    digitalWrite(RED_LED, HIGH);
    sendMQTTMessage("0");
  }

  delay(3000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(doorOpen ? "Door OPEN" : "Door CLOSED");
  digitalWrite(GREEN_LED, LOW);
  digitalWrite(RED_LED, LOW);
}
