/* Progetto: Irrigatore con ESP32, 2 pompe e 2 sensori umidità
* basato su Irrigatore_Wifi_Sock_Blynk_MQTT_Web
* prevede 2 sensori di umidita 2 pompe. La modalita Auto o Manuale si applica ad entrambe le pompe (oentrambe in Auto o entrambe in Manuale 
* utilizza come interfacce Blynk e una pagina html irrig_2pump.html
*/
#include <WiFi.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
//#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1306.h>

// WiFi
const char* ssid = "LZ_24G";
const char* password = "*andromedA01.";

// MQTT
const char* mqtt_server = "192.168.1.104";
const int mqtt_port = 1883;
const char* mqtt_user = "pi";
const char* mqtt_pass = "$oklaM01";
WiFiClient espClient_B;
PubSubClient client(espClient_B);

// Blynk
#define BLYNK_TEMPLATE_ID "TMPL4ntYG9Ybb"
#define BLYNK_TEMPLATE_NAME "ESP32 A"
#define BLYNK_AUTH_TOKEN "NB7Wxvij1bbuTrrPjYfaFo8-yMkfq46U"
#include <BlynkSimpleEsp32.h>
BlynkTimer timer;

// OLED

/*#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
*/

// OneWire
#define ONE_WIRE_BUS 4
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Pin definizione
const int pinPompa1 = 26;
const int pinHumidity1 = 34;
const int pinPompa2 = 25;
const int pinHumidity2 = 35;

// Stato pompe
bool pompa1Attiva = false;
bool pompa2Attiva = false;
unsigned long tempoAvvioPompa1 = 0;
unsigned long tempoAvvioPompa2 = 0;
const int durataPompa = 5000;

int HumidityLev1, mapHumidityLev1;
int HumidityLev2, mapHumidityLev2;
float temperaturaC;

bool modalitaAutomatica = false;
const int MinHumidity = 70;

unsigned long ultimoAvvioPompa1 = 0;
unsigned long ultimoAvvioPompa2 = 0;
const unsigned long intervalloMinimo = 10000; // 10 secondi

void setup_wifi() {
  delay(10);
  Serial.println("Connessione WiFi...");
  pinMode(2, INPUT_PULLUP);                                            
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connesso");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void reconnectMQTT() {
  while (!client.connected()) {
    if (client.connect("ESP32Client_B", mqtt_user, mqtt_pass)) {
      client.subscribe("irrigator2p/comando/pompa1");
      client.subscribe("irrigator2p/comando/pompa2");
      client.subscribe("irrigator2p/comando/modalita");      // <-- AGGIUNTO
      client.subscribe("irrigator2p/get/stato");
    } else {
      delay(5000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  if (String(topic) == "irrigator2p/comando/pompa1") {
    if (!modalitaAutomatica) {
      if (msg == "on") attivaPompa1();
      else disattivaPompa1();
    }
  }

  if (String(topic) == "irrigator2p/comando/pompa2") {
    if (!modalitaAutomatica) {
      if (msg == "on") attivaPompa2();
      else disattivaPompa2();
    }
  }

  if (String(topic) == "irrigator2p/comando/modalita") {
//      Serial.print("Modalità ricevuta via MQTT: ");
//      Serial.println(msg);  // Stampa "auto" o "manuale"
    if (msg == "auto") {
      modalitaAutomatica = true;
      Serial.print("Modalità ricevuta via MQTT: ");
      Serial.println(msg);  // Stampa "auto" o "manuale"
      Blynk.virtualWrite(V3, 1);
    } else if (msg == "manuale") {
      modalitaAutomatica = false;
      Serial.print("Modalità ricevuta via MQTT: ");
      Serial.println(msg);  // Stampa "auto" o "manuale"
      Blynk.virtualWrite(V3, 0);
    }
    client.publish("irrigator2p/stato/modalita", modalitaAutomatica ? "auto" : "manuale");
  }

  if (String(topic) == "irrigator2p/get/stato") {
    client.publish("irrigator2p/stato/modalita", modalitaAutomatica ? "auto" : "manuale");
    client.publish("irrigator2p/stato/pompa1", pompa1Attiva ? "on" : "off");
    client.publish("irrigator2p/stato/pompa2", pompa2Attiva ? "on" : "off");
    client.publish("irrigator2p/stato/map_humidity1", String(mapHumidityLev1).c_str());
    client.publish("irrigator2p/stato/map_humidity2", String(mapHumidityLev2).c_str());
    client.publish("irrigator2p/stato/temperatura", String(temperaturaC).c_str());
  }
}
void attivaPompa1() {
  digitalWrite(pinPompa1, HIGH);
  pompa1Attiva = true;
  tempoAvvioPompa1 = millis();
  Blynk.virtualWrite(V0, 1);
  client.publish("irrigator2p/stato/pompa1", "on");
}
void disattivaPompa1() {
  digitalWrite(pinPompa1, LOW);
  pompa1Attiva = false;
  Blynk.virtualWrite(V0, 0);
  client.publish("irrigator2p/stato/pompa1", "off");
}
void attivaPompa2() {
  digitalWrite(pinPompa2, HIGH);
  pompa2Attiva = true;
  tempoAvvioPompa2 = millis();
  Blynk.virtualWrite(V5, 1);
  client.publish("irrigator2p/stato/pompa2", "on");
}
void disattivaPompa2() {
  digitalWrite(pinPompa2, LOW);
  pompa2Attiva = false;
  Blynk.virtualWrite(V5, 0);
  client.publish("irrigator2p/stato/pompa2", "off");
}

void inviaDati() {
  // Sensore 1
  HumidityLev1 = analogRead(pinHumidity1);
  mapHumidityLev1 = constrain(map(HumidityLev1, 1300, 3500, 0, 100), 0, 100);

  Blynk.virtualWrite(V1, mapHumidityLev1);
  Blynk.virtualWrite(V2, HumidityLev1);
  
  client.publish("irrigator2p/stato/map_humidity1", String(mapHumidityLev1).c_str());
  client.publish("irrigator2p/stato/raw_humidity1", String(HumidityLev1).c_str());

  // Sensore 2
  HumidityLev2 = 3000; // analogRead(pinHumidity2);
  mapHumidityLev2 = constrain(map(HumidityLev2, 1300, 3500, 0, 100), 0, 100);
  
  Blynk.virtualWrite(V6, mapHumidityLev2);
  Blynk.virtualWrite(V7, HumidityLev2);
  client.publish("irrigator2p/stato/map_humidity2", String(mapHumidityLev2).c_str());
  client.publish("irrigator2p/stato/raw_humidity2", String(HumidityLev2).c_str());

  // Temperatura
  sensors.requestTemperatures();
  temperaturaC = sensors.getTempCByIndex(0);
  Blynk.virtualWrite(V4, temperaturaC);
  client.publish("irrigator2p/stato/temperatura", String(temperaturaC).c_str());

  // OLED
  /*
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.printf("T: %.1fC\n", temperaturaC);
  display.printf("H1: %d%%\n", mapHumidityLev1);
  display.printf("H2: %d%%\n", mapHumidityLev2);
  display.printf("P1: %s\n", pompa1Attiva ? "ON" : "OFF");
  display.printf("P2: %s\n", pompa2Attiva ? "ON" : "OFF");
  display.printf("Mode: %s", modalitaAutomatica ? "Auto" : "manuale");
  display.display();
  */
}

BLYNK_WRITE(V3) {
  modalitaAutomatica = param.asInt();
  client.publish("irrigator2p/stato/modalita", modalitaAutomatica ? "auto" : "manuale");
}
BLYNK_WRITE(V0) { if (!modalitaAutomatica) param.asInt() == 1 ? attivaPompa1() : disattivaPompa1(); }
BLYNK_WRITE(V5) { if (!modalitaAutomatica) param.asInt() == 1 ? attivaPompa2() : disattivaPompa2(); }

void setup() {
  pinMode(pinPompa1, OUTPUT);
  pinMode(pinPompa2, OUTPUT);
  digitalWrite(pinPompa1, LOW);
  digitalWrite(pinPompa2, LOW);

  Serial.begin(115200);
  sensors.begin();
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  // Imposta modalità manuale all’avvio
  modalitaAutomatica = false;

  // Dopo che il client MQTT è connesso
  if (!client.connected()) reconnectMQTT();
  Serial.print("MQTT Connected");
  client.publish("irrigator2p/stato/modalita", "manuale");
  
  
  /*
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
*/
  timer.setInterval(3000L, inviaDati);
  inviaDati();
}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();
  Blynk.run();
  timer.run();

  if (modalitaAutomatica) {
//    Serial.println("Modalità Automatica ON");
//
//    Serial.printf("H1: %d%%, Pompa1: %s, millis: %lu, ultimoAvvio: %lu\n",
//      mapHumidityLev1, pompa1Attiva ? "ON" : "OFF", millis(), ultimoAvvioPompa1);

    // Pompa 1
    if (!pompa1Attiva && mapHumidityLev1 < MinHumidity && millis() - ultimoAvvioPompa1 > intervalloMinimo) {
      Serial.println("Attivo Pompa 1");
      attivaPompa1();
      ultimoAvvioPompa1 = millis();
    }
    if (pompa1Attiva && millis() - tempoAvvioPompa1 >= durataPompa) {
      Serial.println("Spengo Pompa 1");
      disattivaPompa1();
    }

    // Pompa 2
    if (!pompa2Attiva && mapHumidityLev2 < MinHumidity && millis() - ultimoAvvioPompa2 > intervalloMinimo) {
      Serial.println("Attivo Pompa 2");
      attivaPompa2();
      ultimoAvvioPompa2 = millis();
    }
    if (pompa2Attiva && millis() - tempoAvvioPompa2 >= durataPompa) {
      Serial.println("Spengo Pompa 2");
      disattivaPompa2();
    }
  }
}
