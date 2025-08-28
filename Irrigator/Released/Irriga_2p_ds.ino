//==============================================================================================
// Irriga_2p_ds.ino 
// Versione: 1.0
// Author: Setzi
// Date: 22-08-2025
// Description: Versione con Deep Sleep per un sistema di irrigazione a 2 pompe.
// L'ESP32 si sveglia, rimane attivo per un tempo definito, e poi torna a dormire.
// Durante il tempo attivo, può ricevere comandi MQTT per accendere/spegnere le pompe.
// Registra il numero di avvii e la durata totale di attivazione delle pompe.
// Le pompe si spengono automaticamente alla fine del periodo attivo se non sono già spente.
// Se il WiFi non si connette al primo avvio, resta attivo per il tempo definito per permettere il debug.
// Se il WiFi non si connette nei successivi avvii, va direttamente in Deep Sleep.
// Se una pompa è accesa alla fine del periodo attivo, aspetta che si spenga prima di andare in Deep Sleep.
// Memorizza la durata totale di attivazione delle pompe tra i cicli di sonno.
// Utilizza MQTT per comunicare con Home Assistant.
// Compatibile con sensori DHT22 e sensori di umidità del terreno capacitivi.
// Configurazione tramite variabili all'inizio del codice.  
// Richiede librerie: WiFi, PubSubClient, DHT, time.h

//================================================================================================

#include <WiFi.h>
#include <PubSubClient.h>
#include "DHT.h"
#include "time.h" // Libreria integrata per NTP e gestione tempo

// --- CONFIGURAZIONE RETE ---
const char* WIFI_SSID = "Dart-24G";
const char* WIFI_PASS = "SoloPerLaFamiglia";
const char* MQTT_BROKER = "192.168.1.103";
const int   MQTT_PORT = 1883;
const char* MQTT_CLIENT_ID = "irrig-2p_ds-esphome";

//
//IPAddress local_IP(192, 168, 1, 212); // Scegli un IP libero
//IPAddress gateway(192, 168, 1, 1);   // L'IP del tuo router
//IPAddress subnet(255, 255, 255, 0);  // Di solito è questa

// --- CONFIGURAZIONE PIN ---
#define PUMP1_PIN 25
#define PUMP2_PIN 27
#define DHT_PIN   13
#define CAP1_PIN  32
#define CAP2_PIN  35

// --- CONFIGURAZIONE TOPIC MQTT ---
const char* STATUS_TOPIC = "irrig-2p_ds/status";
const char* TEMP_AIR_TOPIC = "irrig-2p_ds/sensor/temperatura_aria/state";
const char* HUM_AIR_TOPIC = "irrig-2p_ds/sensor/umidita_aria/state";
const char* SOIL1_PERC_TOPIC = "irrig-2p_ds/sensor/umidita_terreno_1/state";
const char* SOIL2_PERC_TOPIC = "irrig-2p_ds/sensor/umidita_terreno_2/state";
const char* PUMP1_STATE_TOPIC = "irrig-2p_ds/switch/pompa_1/state";
const char* PUMP1_CMD_TOPIC = "irrig-2p_ds/switch/pompa_1/command";
const char* PUMP2_STATE_TOPIC = "irrig-2p_ds/switch/pompa_2/state";
const char* PUMP2_CMD_TOPIC = "irrig-2p_ds/switch/pompa_2/command";
const char* REFRESH_CMD_TOPIC = "irrig-1p/button/updsensordata/command"; // Mantenuto per compatibilità
const char* LAST_BOOT_TOPIC = "irrig-2p_ds/sensor/last_boot/state";
// Rinominati per maggiore chiarezza: ora sono i totali cumulativi
const char* PUMP1_TOTAL_DURATION_TOPIC = "irrig-2p_ds/sensor/pompa_1/total_duration/state";
const char* PUMP2_TOTAL_DURATION_TOPIC = "irrig-2p_ds/sensor/pompa_2/total_duration/state";

// --- CONFIGURAZIONE DEEP SLEEP E NTP ---
const unsigned int CYCLE_TIME_MINUTES = 60; // Durata totale del ciclo (es. 60 min)
const unsigned int ACTIVE_TIME_MINUTES = 5;  // Tempo di attività in minuti (es. 5 min)
const unsigned long ACTIVE_TIME_MS = ACTIVE_TIME_MINUTES * 60 * 1000;
unsigned long awakeStartTime = 0;

const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 3600; // Italia: UTC+1
const int   daylightOffset_sec = 3600; // Ora legale

// --- VALORI DI CALIBRAZIONE ---
const float SOIL1_VOLT_ASCIUTTO = 3.0;
const float SOIL1_VOLT_BAGNATO  = 1.25;
const float SOIL2_VOLT_ASCIUTTO = 2.88;
const float SOIL2_VOLT_BAGNATO  = 1.25;

// --- OGGETTI E VARIABILI GLOBALI ---
WiFiClient espClient;
PubSubClient client(espClient);
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

// --- VARIABILI CHE SOPRAVVIVONO AL DEEP SLEEP ---
RTC_DATA_ATTR int bootCount = 0;
RTC_DATA_ATTR unsigned long totalPump1OnTimeSeconds = 0;
RTC_DATA_ATTR unsigned long totalPump2OnTimeSeconds = 0;

// Variabili per il calcolo della durata (si resettano a ogni risveglio)
unsigned long pump1_startTime_ms = 0;
unsigned long pump2_startTime_ms = 0;

// --- PROTOTIPI FUNZIONI ---
void publishData();
void goToDeepSleep();


float mapfloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  Serial.printf("Messaggio ricevuto [%s]: %s\n", topic, message.c_str());

  if (strcmp(topic, PUMP1_CMD_TOPIC) == 0) {
    if (message == "ON") {
      digitalWrite(PUMP1_PIN, HIGH);
      client.publish(PUMP1_STATE_TOPIC, "ON", true);
      pump1_startTime_ms = millis(); // Avvia cronometro
    } else if (message == "OFF") {
      digitalWrite(PUMP1_PIN, LOW);
      client.publish(PUMP1_STATE_TOPIC, "OFF", true);
      if (pump1_startTime_ms > 0) {
        unsigned long durationMs = millis() - pump1_startTime_ms;
        totalPump1OnTimeSeconds += durationMs / 1000;
        char buffer[20];
        sprintf(buffer, "%lu", totalPump1OnTimeSeconds);
        client.publish(PUMP1_TOTAL_DURATION_TOPIC, buffer, true);
        Serial.printf("Pompa 1: Durata sessione: %lu s. Totale: %lu s.\n", durationMs / 1000, totalPump1OnTimeSeconds);
        pump1_startTime_ms = 0;
      }
    }
  }
  else if (strcmp(topic, PUMP2_CMD_TOPIC) == 0) {
    if (message == "ON") {
      digitalWrite(PUMP2_PIN, HIGH);
      client.publish(PUMP2_STATE_TOPIC, "ON", true);
      pump2_startTime_ms = millis(); // Avvia cronometro
    } else if (message == "OFF") {
      digitalWrite(PUMP2_PIN, LOW);
      client.publish(PUMP2_STATE_TOPIC, "OFF", true);
      if (pump2_startTime_ms > 0) {
        unsigned long durationMs = millis() - pump2_startTime_ms;
        totalPump2OnTimeSeconds += durationMs / 1000;
        char buffer[20];
        sprintf(buffer, "%lu", totalPump2OnTimeSeconds);
        client.publish(PUMP2_TOTAL_DURATION_TOPIC, buffer, true);
        Serial.printf("Pompa 2: Durata sessione: %lu s. Totale: %lu s.\n", durationMs / 1000, totalPump2OnTimeSeconds);
        pump2_startTime_ms = 0;
      }
    }
  }
  else if (strcmp(topic, REFRESH_CMD_TOPIC) == 0) {
    Serial.println("Richiesta di aggiornamento manuale ricevuta!");
    publishData();
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Tentativo di connessione MQTT...");
    if (client.connect(MQTT_CLIENT_ID, nullptr, nullptr, STATUS_TOPIC, 0, true, "offline")) {
      Serial.println("connesso!");
      client.publish(STATUS_TOPIC, "online", true);
      client.subscribe(PUMP1_CMD_TOPIC);
      client.subscribe(PUMP2_CMD_TOPIC);
      client.subscribe(REFRESH_CMD_TOPIC);

      // Pubblica stati iniziali al risveglio
      client.publish(PUMP1_STATE_TOPIC, digitalRead(PUMP1_PIN) ? "ON" : "OFF", true);
      client.publish(PUMP2_STATE_TOPIC, digitalRead(PUMP2_PIN) ? "ON" : "OFF", true);
      
      char buffer[20];
      sprintf(buffer, "%lu", totalPump1OnTimeSeconds);
      client.publish(PUMP1_TOTAL_DURATION_TOPIC, buffer, true);
      sprintf(buffer, "%lu", totalPump2OnTimeSeconds);
      client.publish(PUMP2_TOTAL_DURATION_TOPIC, buffer, true);

    } else {
      Serial.printf(" fallito, rc=%d. Riprovo tra 5 secondi\n", client.state());
      delay(5000);
    }
  }
}

void publishData() {
  Serial.println("--- Pubblicazione dati sensori ---");

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  if (!isnan(temp) && !isnan(hum)) {
    client.publish(TEMP_AIR_TOPIC, String(temp, 1).c_str(), true);
    client.publish(HUM_AIR_TOPIC, String(hum, 1).c_str(), true);
    Serial.printf("DHT22: %.1f°C, %.1f%%\n", temp, hum);
  }

  float volt1 = (analogRead(CAP1_PIN) / 4095.0) * 3.3;
  float perc1 = mapfloat(volt1, SOIL1_VOLT_ASCIUTTO, SOIL1_VOLT_BAGNATO, 0.0, 100.0);
  perc1 = constrain(perc1, 0, 100);
  client.publish(SOIL1_PERC_TOPIC, String(perc1, 1).c_str(), true);
  Serial.printf("Sensore 1: %.2fV -> %.1f%%\n", volt1, perc1);

  float volt2 = (analogRead(CAP2_PIN) / 4095.0) * 3.3;
  float perc2 = mapfloat(volt2, SOIL2_VOLT_ASCIUTTO, SOIL2_VOLT_BAGNATO, 0.0, 100.0);
  perc2 = constrain(perc2, 0, 100);
  client.publish(SOIL2_PERC_TOPIC, String(perc2, 1).c_str(), true);
  Serial.printf("Sensore 2: %.2fV -> %.1f%%\n", volt2, perc2);
}

void goToDeepSleep() {
    const long sleepDurationMinutes = CYCLE_TIME_MINUTES - ACTIVE_TIME_MINUTES;
    long sleepTimeSeconds = sleepDurationMinutes * 60;
    
    Serial.printf("Tempo di attività terminato. Vado a dormire per %ld minuti.\n", sleepDurationMinutes);
    
    if (sleepTimeSeconds <= 0) {
        Serial.println("ERRORE: Tempo di sonno non valido! Riavvio tra 10 secondi...");
        delay(10000);
        ESP.restart();
    }

    esp_sleep_enable_timer_wakeup(sleepTimeSeconds * 1000000ULL);
    
    Serial.println("Disconnessione e avvio deep sleep...");
    client.disconnect();
    WiFi.disconnect(true);
    
    esp_deep_sleep_start();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  ++bootCount;
  Serial.printf("\n============================\nAVVIO NUMERO: %d\n============================\n", bootCount);

  pinMode(PUMP1_PIN, OUTPUT);
  pinMode(PUMP2_PIN, OUTPUT);
  digitalWrite(PUMP1_PIN, LOW);
  digitalWrite(PUMP2_PIN, LOW);
  analogSetAttenuation(ADC_11db);
  dht.begin();
  
  awakeStartTime = millis();

  // Connessione WiFi
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connessione a WiFi...");
  int wifi_retries = 30;
  while (WiFi.status() != WL_CONNECTED && wifi_retries > 0) {
    delay(500);
    Serial.print(".");
    wifi_retries--;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nConnessione Wi-Fi fallita.");
    if (bootCount > 1) { // Se non è il primo avvio, torna a dormire
      goToDeepSleep();
    } else { // Al primo avvio, resta sveglio per il debug
      Serial.println("Primo avvio: resto attivo per la durata prefissata anche senza WiFi.");
    }
  } else {
    Serial.println("\nWiFi connesso!");
    Serial.print("Indirizzo IP: ");
    Serial.println(WiFi.localIP());

    // Configura e sincronizza NTP
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Connessione MQTT
    client.setServer(MQTT_BROKER, MQTT_PORT);
    client.setCallback(callback);
    reconnect();

    // Pubblica ora di avvio
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeString[25];
      strftime(timeString, sizeof(timeString), "%Y-%m-%d %H:%M:%S", &timeinfo);
      client.publish(LAST_BOOT_TOPIC, timeString, true);
    }
    
    // Pubblica tutti i dati al risveglio
    publishData();
  }
}

void loop() {
  if (millis() - awakeStartTime > ACTIVE_TIME_MS) {
    // Controlla se una delle due pompe è ancora accesa
    if (digitalRead(PUMP1_PIN) == HIGH || digitalRead(PUMP2_PIN) == HIGH) {
      static bool waiting_message_printed = false;
      if (!waiting_message_printed) {
        Serial.println("Tempo attività scaduto, ma almeno una pompa è accesa. Attendo spegnimento...");
        waiting_message_printed = true;
      }
    } else {
      // Entrambe le pompe sono spente, possiamo andare a dormire
      goToDeepSleep();
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  }

  delay(50); // Pausa per stabilità
}
