// =================================================================
// ==         CODICE ARDUINO PER IRRIGATORE 2P                   ==
// ==                 Aggiunta comando refresh manuale           ==
// ==       Ora di Attivazione Pompe e Durata Attivazione        
// =================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <time.h>

// --- CONFIGURAZIONE RETE ---
const char* WIFI_SSID = "Dart-24G";
const char* WIFI_PASS = "SoloPerLaFamiglia";
const char* MQTT_BROKER = "192.168.1.103";
const int   MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "irrigator2ps_arduino";

// --- CONFIGURAZIONE PIN ---
//Pin ALTO (HIGH / 5V) = Pompa ACCESA
//Pin BASSO (LOW / 0V) = Pompa SPENTA
#define PUMP1_PIN 25
#define PUMP2_PIN 26

#define DHT_PIN   27
#define CAP1_PIN  32
#define CAP2_PIN  35

// --- CONFIGURAZIONE TOPIC MQTT ---
const char* STATUS_TOPIC = "irrig-2p/status";
const char* TEMP_AIR_TOPIC = "irrig-2p/sensor/temperatura_aria/state";
const char* HUM_AIR_TOPIC = "irrig-2p/sensor/umidita_aria/state";
const char* SOIL1_RAW_TOPIC = "irrig-2p/sensor/umidita_terreno_1_raw/state";
const char* SOIL1_PERC_TOPIC = "irrig-2p/sensor/umidita_terreno_1/state";
const char* SOIL2_RAW_TOPIC = "irrig-2p/sensor/umidita_terreno_2_raw/state";
const char* SOIL2_PERC_TOPIC = "irrig-2p/sensor/umidita_terreno_2/state";
const char* PUMP1_STATE_TOPIC = "irrig-2p/switch/pompa_1/state";
const char* PUMP1_CMD_TOPIC = "irrig-2p/switch/pompa_1/command";
const char* PUMP2_STATE_TOPIC = "irrig-2p/switch/pompa_2/state";
const char* PUMP2_CMD_TOPIC = "irrig-2p/switch/pompa_2/command";
const char* REFRESH_CMD_TOPIC = "irrig-1p/button/updsensordata/command";
const char* LAST_REFRESH_TOPIC = "irrig-2p/sensor/last_refresh/state";
const char* PUMP1_LAST_ACTIVATION_TOPIC = "irrig-2p/switch/pompa_1/last_activation/state";
const char* PUMP2_LAST_ACTIVATION_TOPIC = "irrig-2p/switch/pompa_2/last_activation/state";

// <-- MODIFICA: Nuovi topic per la durata di attivazione
const char* PUMP1_LAST_DURATION_TOPIC = "irrig-2p/switch/pompa_1/last_duration/state";
const char* PUMP2_LAST_DURATION_TOPIC = "irrig-2p/switch/pompa_2/last_duration/state";

// --- CONFIGURAZIONE NTP ---
// L'offset viene impostato a 0. L'ESP32 lavorerà in UTC.
// La conversione nel fuso orario locale verrà gestita dal browser.
const long  NTP_OFFSET = 0;

// ===============================================================
// ==           DA PERSONALIZZARE: VALORI DI CALIBRAZIONE       ==
// ===============================================================
const float SOIL1_VOLT_ASCIUTTO = 3.0;
const float SOIL1_VOLT_BAGNATO  = 1.25;
const float SOIL2_VOLT_ASCIUTTO = 2.88;
const float SOIL2_VOLT_BAGNATO  = 1.25;

// --- OGGETTI E VARIABILI GLOBALI ---
WiFiClient espClient;
PubSubClient client(espClient);
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", NTP_OFFSET);

// <-- MODIFICA: Variabili per memorizzare l'ora di avvio delle pompe
unsigned long pump1_startTime = 0;
unsigned long pump2_startTime = 0;

unsigned long previousMillis = 0;
const long interval = 10000;

void publishData();

float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// <-- MODIFICA: Funzione per formattare la durata in un formato leggibile
String formatDuration(unsigned long seconds) {
    long minutes = seconds / 60;
    long remainingSeconds = seconds % 60;
    return String(minutes) + " min " + String(remainingSeconds) + " sec";
}

// <-- MODIFICA: Funzione aggiornata per inviare il timestamp Unix grezzo
void publishActivationTime(int pump_number) {
    timeClient.update();
    // 1. Ottieni il timestamp Unix (un numero che rappresenta i secondi dal 1970)
    unsigned long epochTime = timeClient.getEpochTime();
    
    // 2. Converti questo numero in una stringa
    char timestampString[12];
    sprintf(timestampString, "%lu", epochTime); // %lu è per unsigned long

    // 3. Pubblica la stringa numerica sul topic corretto
    if (pump_number == 1) {
        client.publish(PUMP1_LAST_ACTIVATION_TOPIC, timestampString, true);
        Serial.printf("Pompa 1: Pubblicato timestamp di attivazione: %s\n", timestampString);
    } else if (pump_number == 2) {
        client.publish(PUMP2_LAST_ACTIVATION_TOPIC, timestampString, true);
        Serial.printf("Pompa 2: Pubblicato timestamp di attivazione: %s\n", timestampString);
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }

  Serial.print("Messaggio ricevuto sul topic: ");
  Serial.println(topic);

  timeClient.update(); // Aggiorniamo l'ora per averla pronta

  if (strcmp(topic, PUMP1_CMD_TOPIC) == 0) {
    if (message == "ON") {
      digitalWrite(PUMP1_PIN, HIGH);
      client.publish(PUMP1_STATE_TOPIC, "ON", true);
      publishActivationTime(1);
      // <-- MODIFICA: Avvia il cronometro per la pompa 1
      pump1_startTime = timeClient.getEpochTime();
    } else if (message == "OFF") {
      digitalWrite(PUMP1_PIN, LOW);
      client.publish(PUMP1_STATE_TOPIC, "OFF", true);
      // <-- MODIFICA: Ferma il cronometro, calcola e pubblica la durata
      if (pump1_startTime > 0) {
        unsigned long duration = timeClient.getEpochTime() - pump1_startTime;
        String formattedDuration = formatDuration(duration);
        client.publish(PUMP1_LAST_DURATION_TOPIC, formattedDuration.c_str(), true);
        Serial.printf("Pompa 1 disattivata. Durata attività: %s\n", formattedDuration.c_str());
        pump1_startTime = 0; // Resetta il timer
      }
    }
  }
  else if (strcmp(topic, PUMP2_CMD_TOPIC) == 0) {
    if (message == "ON") {
      digitalWrite(PUMP2_PIN, HIGH);
      client.publish(PUMP2_STATE_TOPIC, "ON", true);
      publishActivationTime(2);
      // <-- MODIFICA: Avvia il cronometro per la pompa 2
      pump2_startTime = timeClient.getEpochTime();
    } else if (message == "OFF") {
      digitalWrite(PUMP2_PIN, LOW);
      client.publish(PUMP2_STATE_TOPIC, "OFF", true);
      // <-- MODIFICA: Ferma il cronometro, calcola e pubblica la durata
      if (pump2_startTime > 0) {
        unsigned long duration = timeClient.getEpochTime() - pump2_startTime;
        String formattedDuration = formatDuration(duration);
        client.publish(PUMP2_LAST_DURATION_TOPIC, formattedDuration.c_str(), true);
        Serial.printf("Pompa 2 disattivata. Durata attività: %s\n", formattedDuration.c_str());
        pump2_startTime = 0; // Resetta il timer
      }
    }
  }
  else if (strcmp(topic, REFRESH_CMD_TOPIC) == 0) {
    Serial.println("Richiesta di aggiornamento manuale ricevuta! Forzo la pubblicazione dei dati...");
    publishData();
  }
}

// ... le altre funzioni (setup_wifi, reconnect, publishData, setup, loop) rimangono invariate ...
void setup_wifi() {
  delay(10);
  Serial.begin(115200);
  Serial.println();
  Serial.print("Connessione a: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connesso!");
  Serial.print("Indirizzo IP: ");
  Serial.println(WiFi.localIP());
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentativo di connessione MQTT...");
    if (client.connect(MQTT_CLIENT_ID)) {
      Serial.println("connesso!");
      client.publish(STATUS_TOPIC, "online", true);
      client.subscribe(PUMP1_CMD_TOPIC);
      client.subscribe(PUMP2_CMD_TOPIC);
      client.subscribe(REFRESH_CMD_TOPIC);
    } else {
      Serial.print("fallito, rc=");
      Serial.print(client.state());
      Serial.println(" Riprovo tra 5 secondi");
      delay(5000);
    }
  }
}

void publishData() {
  Serial.println("--- Inizio pubblicazione dati sensori ---");

  timeClient.update();
  String formattedTime = timeClient.getFormattedTime();
  client.publish(LAST_REFRESH_TOPIC, formattedTime.c_str(), true);
  Serial.printf("Orario aggiornamento sensori: %s\n", formattedTime.c_str());

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  if (!isnan(temp) && !isnan(hum)) {
    client.publish(TEMP_AIR_TOPIC, String(temp).c_str(), true);
    client.publish(HUM_AIR_TOPIC, String(hum).c_str(), true);
    Serial.printf("DHT22: %.1f°C, %.1f%%\n", temp, hum);
  }

  // Sensore 1
  float volt1 = (analogRead(CAP1_PIN) / 4095.0) * 3.3;
  float perc1 = mapfloat(volt1, SOIL1_VOLT_ASCIUTTO, SOIL1_VOLT_BAGNATO, 0.0, 100.0);
  perc1 = constrain(perc1, 0, 100);
  client.publish(SOIL1_RAW_TOPIC, String(volt1).c_str(), true);
  client.publish(SOIL1_PERC_TOPIC, String(perc1).c_str(), true);
  Serial.printf("Sensore 1: %.2fV -> %.1f%%\n", volt1, perc1);

  // Sensore 2
  float volt2 = (analogRead(CAP2_PIN) / 4095.0) * 3.3;
  float perc2 = mapfloat(volt2, SOIL2_VOLT_ASCIUTTO, SOIL2_VOLT_BAGNATO, 0.0, 100.0);
  perc2 = constrain(perc2, 0, 100);
  client.publish(SOIL2_RAW_TOPIC, String(volt2).c_str(), true);
  client.publish(SOIL2_PERC_TOPIC, String(perc2).c_str(), true);
  Serial.printf("Sensore 2: %.2fV -> %.1f%%\n", volt2, perc2);

  Serial.println("--- Fine pubblicazione dati sensori ---");
}

void setup() {
  pinMode(PUMP1_PIN, OUTPUT);
  pinMode(PUMP2_PIN, OUTPUT);
  digitalWrite(PUMP1_PIN, LOW);
  digitalWrite(PUMP2_PIN, LOW);

  analogSetAttenuation(ADC_11db);
  dht.begin();
  
  setup_wifi();
  
  timeClient.begin();
  
  client.setServer(MQTT_BROKER, MQTT_PORT);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    publishData();
  }
}
