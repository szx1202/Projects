// =====================================================================
// ==   File: Irriga_3ps_od.ino (Versione per 3 Pompe e 3 Sensori)
// ==   Gestisce 3 pompe, 3 sensori di umidità terreno e 1 DHT22
// ==   via WiFi e MQTT.
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
const char* mqtt_client_id = "esp32-irrigator-client-3p-od";

// --- NUOVO: Numero di pompe/sensori da gestire ---
const int NUM_DEVICES = 3;

// --- Definizione Centralizzata dei Topic MQTT ---
// Base del topic per questo dispositivo
const char* MQTT_BASE_TOPIC = "irrig-3p"; 

// Topic globali (non specifici per una singola pompa/sensore)
const char* TEMP_AIR_STATE_TOPIC = "irrig-3p/sensor/air_temperature/state";
const char* HUMIDITY_AIR_STATE_TOPIC = "irrig-3p/sensor/air_humidity/state";
const char* DATA_REQUEST_COMMAND_TOPIC = "irrig-3p/button/updsensordata/command";

// --- Configurazione dei Pin GPIO ---
// Array per i pin delle pompe
const int PUMP_PINS[NUM_DEVICES] = {25, 26, 33}; // Esempio: GPIO26 per Pompa 1, GPIO25 per Pompa 2, etc.
// Array per i pin dei sensori di umidità del terreno (devono essere pin ADC)
const int SOIL_MOISTURE_PINS[NUM_DEVICES] = {32, 34, 35}; // Esempio: GPIO32 per Sensore 1, etc.
// Pin per il sensore DHT22 (rimane singolo)
const int DHT_PIN = 27;

// --- Configurazione dei Sensori ---
#define DHTTYPE DHT22
DHT dht(DHT_PIN, DHTTYPE);

// --- Calibrazione Sensori Umidità Terreno (ADC) ---
// Ora abbiamo un array per i valori di calibrazione di ogni sensore
// Valore letto quando il sensore è completamente asciutto (in aria)
const int SENSOR_DRY_VALUES[NUM_DEVICES] = {3400, 3410, 3390}; // Calibrare ogni sensore individualmente
// Valore letto quando il sensore è immerso nell'acqua
const int SENSOR_WET_VALUES[NUM_DEVICES] = {2873, 1320, 1290}; // Calibrare ogni sensore individualmente

// --- Inizializzazione Client ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Variabili per il timing ---
unsigned long lastMsg = 0;
const long interval = 3600000; // Intervallo 1h per la pubblicazione automatica

// ==========================================================
// ==   FUNZIONE PER LEGGERE E PUBBLICARE I DATI SENSORI  ==
// ==========================================================
void readAndPublishSensors() {
  Serial.println("--- Eseguo lettura e pubblicazione dati sensori ---");

  // 1. Sensore DHT22 (una sola volta)
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

  // 2. Sensori Umidità Terreno (ciclo per tutti i sensori)
  for (int i = 0; i < NUM_DEVICES; i++) {
    int soilValue = analogRead(SOIL_MOISTURE_PINS[i]);
    int soilMoisturePercent = map(soilValue, SENSOR_DRY_VALUES[i], SENSOR_WET_VALUES[i], 0, 100);
    soilMoisturePercent = constrain(soilMoisturePercent, 0, 100);

    // Buffer per costruire il topic dinamicamente
    char topicBuffer[100];
    sprintf(topicBuffer, "%s/sensor/soil_humidity_%d/state", MQTT_BASE_TOPIC, i + 1);

    Serial.print("Sensore Terreno #"); Serial.print(i + 1);
    Serial.print(" | RAW: "); Serial.print(soilValue);
    Serial.print(" | Umidità: "); Serial.print(soilMoisturePercent); Serial.println(" %");
    
    client.publish(topicBuffer, String(soilMoisturePercent).c_str(), true);
  }
}


// =================================================
// ==     FUNZIONE DI CALLBACK PER MQTT           ==
// =================================================
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Messaggio ricevuto sul topic: ");
  Serial.println(topic);

  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  // Gestione richiesta dati on-demand
  if (strcmp(topic, DATA_REQUEST_COMMAND_TOPIC) == 0) {
    Serial.println("Richiesta di aggiornamento dati ricevuta!");
    readAndPublishSensors();
    return; // Usciamo dalla funzione
  }

  // Gestione comandi pompe (ciclo per verificare quale pompa)
  for (int i = 0; i < NUM_DEVICES; i++) {
    char commandTopicBuffer[100];
    sprintf(commandTopicBuffer, "%s/switch/pompa_%d/command", MQTT_BASE_TOPIC, i + 1);

    if (strcmp(topic, commandTopicBuffer) == 0) {
      Serial.print("Comando per Pompa #"); Serial.println(i + 1);
      
      char stateTopicBuffer[100];
      sprintf(stateTopicBuffer, "%s/switch/pompa_%d/state", MQTT_BASE_TOPIC, i + 1);

      if (message == "ON") {
        digitalWrite(PUMP_PINS[i], HIGH);
        client.publish(stateTopicBuffer, "ON", true);
        Serial.print("Pompa #"); Serial.print(i + 1); Serial.println(" ACCESA e stato pubblicato");
      } else if (message == "OFF") {
        digitalWrite(PUMP_PINS[i], LOW);
        client.publish(stateTopicBuffer, "OFF", true);
        Serial.print("Pompa #"); Serial.print(i + 1); Serial.println(" SPENTA e stato pubblicato");
      }
      return; // Usciamo dalla funzione dopo aver gestito il comando
    }
  }
}

// =================================================
// ==     FUNZIONE DI RICONNESSIONE A MQTT        ==
// =================================================
void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentativo di connessione MQTT...");
    if (client.connect(mqtt_client_id)) {
      Serial.println("connesso!");
      
      // Iscriviti al topic di richiesta dati
      client.subscribe(DATA_REQUEST_COMMAND_TOPIC);
      Serial.print("Iscritto a: "); Serial.println(DATA_REQUEST_COMMAND_TOPIC);

      // Iscriviti ai topic di comando per ogni pompa e pubblica lo stato iniziale
      for (int i = 0; i < NUM_DEVICES; i++) {
        char commandTopicBuffer[100];
        char stateTopicBuffer[100];
        
        sprintf(commandTopicBuffer, "%s/switch/pompa_%d/command", MQTT_BASE_TOPIC, i + 1);
        sprintf(stateTopicBuffer, "%s/switch/pompa_%d/state", MQTT_BASE_TOPIC, i + 1);

        // Iscrizione al topic di comando
        client.subscribe(commandTopicBuffer);
        Serial.print("Iscritto a: "); Serial.println(commandTopicBuffer);

        // Pubblicazione dello stato iniziale della pompa (retained)
        if (digitalRead(PUMP_PINS[i]) == HIGH) {
          client.publish(stateTopicBuffer, "ON", true);
        } else {
          client.publish(stateTopicBuffer, "OFF", true);
        }
      }

    } else {
      Serial.print("fallito, rc=");
      Serial.print(client.state());
      Serial.println(" nuovo tentativo tra 5 secondi");
      delay(5000);
    }
  }
}

// =================================================
// ==           SETUP (Eseguito una volta)        ==
// =================================================
void setup() {
  Serial.begin(115200);
  
  // Imposta i pin delle pompe come OUTPUT e li spegne
  for (int i = 0; i < NUM_DEVICES; i++) {
    pinMode(PUMP_PINS[i], OUTPUT);
    digitalWrite(PUMP_PINS[i], LOW);
  }
  
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
