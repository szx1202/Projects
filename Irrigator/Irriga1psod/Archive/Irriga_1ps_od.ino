// =====================================================================
// ==    File: irrigatore.ino (Versione Completa e Funzionante)
// ==    
// =====================================================================

// --- Inclusione delle Librerie Necessarie ---
#include <WiFi.h>
#include <PubSubClient.h> // Per MQTT
#include <DHT.h>          // Per il sensore DHT22
#include <Adafruit_Sensor.h>

// --- Configurazione Wi-Fi ---
const char* ssid = "Dart-24G";
const char* password = "SoloPerLaFamiglia";

// --- Configurazione MQTT ---
const char* mqtt_server = "192.168.1.103";
const int mqtt_port = 1883;
const char* mqtt_client_id = "esp32-irrigator-client-od";

// --- Definizione Centralizzata dei Topic MQTT ---
const char* PUMP_STATE_TOPIC = "irrigator1psod/switch/pompa/state";
const char* PUMP_COMMAND_TOPIC = "irrigator1psod/switch/pompa/command";
const char* TEMP_AIR_STATE_TOPIC = "irrigator1psod/sensor/temperatura_aria/state";
const char* HUMIDITY_AIR_STATE_TOPIC = "irrigator1psod/sensor/umidita_aria/state";
const char* SOIL_MOISTURE_STATE_TOPIC = "irrigator1psod/sensor/umidita_terreno/state";
const char* DATA_REQUEST_COMMAND_TOPIC = "irrigator1psod/data/request/command";

// --- Configurazione dei Pin GPIO ---
const int PUMP_PIN = 26;
const int DHT_PIN = 27;
const int SOIL_MOISTURE_PIN = 32;

// --- Configurazione dei Sensori ---
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

// --- Calibrazione Sensore Umidità Terreno (ADC) ---
const int SENSOR_DRY_VALUE = 2400;
const int SENSOR_WET_VALUE = 1100;

// --- Inizializzazione Client ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Variabili per il timing ---
unsigned long lastMsg = 0;
const long interval = 60000; // Intervallo 1 min per la pubblicazione automatica

// ==========================================================
// ==    FUNZIONE PER LEGGERE E PUBBLICARE I DATI SENSORI  ==
// ==========================================================
void readAndPublishSensors() {
  Serial.println("--- Eseguo lettura e pubblicazione dati sensori ---");

  // 1. Sensore DHT22
  float h = dht.readHumidity();
  float t = dht.readTemperature();

  if (isnan(h) || isnan(t)) {
    Serial.println("Errore lettura dal sensore DHT!");
  } else {
    Serial.print("Umidità Aria: "); Serial.print(h); Serial.print(" %\t");
    Serial.print("Temperatura: "); Serial.print(t); Serial.println(" *C");
    client.publish(TEMP_AIR_STATE_TOPIC, String(t).c_str(), true);
    client.publish(HUMIDITY_AIR_STATE_TOPIC, String(h).c_str(), true);
  }

  // 2. Sensore Umidità Terreno
  int soilValue = analogRead(SOIL_MOISTURE_PIN);
  int soilMoisturePercent = map(soilValue, SENSOR_DRY_VALUE, SENSOR_WET_VALUE, 0, 100);
  soilMoisturePercent = constrain(soilMoisturePercent, 0, 100);

  Serial.print("Umidità Terreno: "); Serial.print(soilMoisturePercent); Serial.println(" %");
  client.publish(SOIL_MOISTURE_STATE_TOPIC, String(soilMoisturePercent).c_str(), true);
}


// =================================================
// ==      FUNZIONE DI CALLBACK PER MQTT          ==
// =================================================
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Messaggio ricevuto sul topic: ");
  Serial.println(topic);

  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  // Gestione comando pompa
  if (strcmp(topic, PUMP_COMMAND_TOPIC) == 0) {
    if (message == "ON") {
      digitalWrite(PUMP_PIN, HIGH);
      client.publish(PUMP_STATE_TOPIC, "ON", true);
      Serial.println("Pompa ACCESA e stato pubblicato");
    } else if (message == "OFF") {
      digitalWrite(PUMP_PIN, LOW);
      client.publish(PUMP_STATE_TOPIC, "OFF", true);
      Serial.println("Pompa SPENTA e stato pubblicato");
    }
  } 
  // Gestione richiesta dati
  else if (strcmp(topic, DATA_REQUEST_COMMAND_TOPIC) == 0) {
    Serial.println("Richiesta di aggiornamento dati ricevuta dalla dashboard!");
    readAndPublishSensors();
  }
}

// =================================================
// ==      FUNZIONE DI RICONNESSIONE A MQTT       ==
// =================================================
// =================================================
// ==      FUNZIONE DI RICONNESSIONE A MQTT       ==
// =================================================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentativo di connessione MQTT...");
    if (client.connect(mqtt_client_id)) {
      Serial.println("connesso!");
      
      // Iscriviti ad entrambi i topic di comando
      client.subscribe(PUMP_COMMAND_TOPIC);
      client.subscribe(DATA_REQUEST_COMMAND_TOPIC);
      Serial.print("Iscritto a: "); Serial.println(PUMP_COMMAND_TOPIC);
      Serial.print("Iscritto a: "); Serial.println(DATA_REQUEST_COMMAND_TOPIC);

      // --- NUOVA PARTE: PUBBLICA LO STATO INIZIALE ---
      // Questo sbloccherà il bottone sulla dashboard alla connessione.
      Serial.println("Pubblicazione dello stato iniziale della pompa...");
      // Legge lo stato fisico del pin e pubblica il messaggio corrispondente.
      if (digitalRead(PUMP_PIN) == HIGH) {
        client.publish(PUMP_STATE_TOPIC, "ON", true);
      } else {
        client.publish(PUMP_STATE_TOPIC, "OFF", true);
      }
      // Il 'true' finale imposta il messaggio come "retained", 
      // così ogni nuovo client che si connette riceverà subito l'ultimo stato.
      // --- FINE NUOVA PARTE ---

    } else {
      Serial.print("fallito, rc=");
      Serial.print(client.state());
      Serial.println(" nuovo tentativo tra 5 secondi");
      delay(5000);
    }
  }
}

// =================================================
// ==             SETUP (Eseguito una volta)      ==
// =================================================
void setup() {
  Serial.begin(115200);
  
  pinMode(PUMP_PIN, OUTPUT);
  digitalWrite(PUMP_PIN, LOW);
  
  dht.begin();

  Serial.println();
  Serial.print("Connessione a ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connesso!");
  Serial.print("Indirizzo IP: ");
  Serial.println(WiFi.localIP());

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

// =================================================
// ==           LOOP (Eseguito continuamente)     ==
// =================================================
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > interval) {
    lastMsg = now;
    readAndPublishSensors();
  }
}
