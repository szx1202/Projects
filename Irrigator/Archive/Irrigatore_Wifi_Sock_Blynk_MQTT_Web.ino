/*  *** Ver. 1.0 ***
 *  The Automatic Irrigation DIY Kit Self Watering System is based on an ESP-32 Dev Kit
 *  This program allows to measure 
 *  1) the Humidity  of the terrain using a capacitive soil moisture sensor for Arduino
 *  2) The Temperature using a DS18B20 sensor
 *  The data are sent to a Mosquitto MQTT Broker and diplayed on a OLED Screen 128X64
 *  A Micro Water Pump is activated via a Relay when the Humidity drops under a fixed value.
 *  There are 2 Working Mode: Manual and Automatic. 
 *  In Manual Mode the Pump can be activated\deactivated manually
 *  In Automatic Mode the pump starts when the Humidity drops under the prefixed value.
 *  The pump can be managed either via a Blynk Dashboard or a Web Page.
 */

#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
//#include <Adafruit_GFX.h>
//#include <Adafruit_SSD1306.h>


// OLED
/*
#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_RESET -1
#define SDA_PIN 27
#define SCL_PIN 25
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET);
*/

// Pin sensori
const int pinPompa = 26;
const int pinHumidity = 34;

// DS18B20

#define ONE_WIRE_BUS 32
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float temperaturaC = 0.0;

// Stato
bool pompaAttiva = false;
bool modalitaAutomatica = false;
unsigned long tempoAvvioPompa = 0;
const int durataPompa = 5000;
const int MinHumidity = 70;

unsigned long ultimoAvvioPompa = 0;
const unsigned long intervalloMinimo = 10000; // 10 secondi

int HumidityLev;
int mapHumidityLev;

// WiFi
const char* ssid = "LZ_24G";
const char* password = "*andromedA01.";

// Blynk
#define BLYNK_TEMPLATE_ID "TMPL4ntYG9Ybb"
#define BLYNK_TEMPLATE_NAME "ESP32 A"
#define BLYNK_AUTH_TOKEN "NB7Wxvij1bbuTrrPjYfaFo8-yMkfq46U"
#include <BlynkSimpleEsp32.h>
BlynkTimer timer;

// MQTT
const char* mqtt_server = "192.168.1.104";
const char* mqtt_user = "pi";
const char* mqtt_password = "$oklaM01";
WiFiClient espClient;
PubSubClient client(espClient);

/************************************************************************************/
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

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  Serial.printf("MQTT ricevuto [%s]: %s\n", topic, msg.c_str());

  if (String(topic) == "irrigatore/comando/modalita") {
    modalitaAutomatica = (msg == "auto");
    Blynk.virtualWrite(V3, modalitaAutomatica ? 1 : 0);
    if (modalitaAutomatica) disattivaPompa();
  }

  if (String(topic) == "irrigatore/comando/pompa") {
    if (!modalitaAutomatica) {
      if (msg == "on") attivaPompa();
      else if (msg == "off") disattivaPompa();
    }
  }

  if (String(topic) == "irrigatore/get/stato") {
    inviaStatoCorrente();
  }
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connessione MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("connesso");
      client.subscribe("irrigatore/comando/modalita");
      client.subscribe("irrigatore/comando/pompa");
      client.subscribe("irrigatore/get/stato");
    } else {
      Serial.print("Errore, rc=");
      Serial.print(client.state());
      Serial.println(" riprovo tra 5s");
      delay(5000);
    }
  }
}

void attivaPompa() {
  digitalWrite(pinPompa, HIGH);
  pompaAttiva = true;
  tempoAvvioPompa = millis();
  Serial.println("Pompa attivata");
  Blynk.virtualWrite(V0, 1);
  client.publish("irrigatore/stato/pompa", "on");
}

void disattivaPompa() {
  digitalWrite(pinPompa, LOW);
  pompaAttiva = false;
  Serial.println("Pompa disattivata");
  Blynk.virtualWrite(V0, 0);
  client.publish("irrigatore/stato/pompa", "off");
}

void inviaDati() {
  HumidityLev = analogRead(pinHumidity);
  mapHumidityLev = map(HumidityLev, 3500, 1300, 0, 100);
  mapHumidityLev = constrain(mapHumidityLev, 0, 100);
  //mapHumidityLev = constrain(map(HumidityLev, 1300, 3500, 0, 100), 0, 100);
  Blynk.virtualWrite(V1, mapHumidityLev);
  Blynk.virtualWrite(V2, HumidityLev);

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", mapHumidityLev);
  client.publish("irrigatore/stato/map_humidity", buf);
  Serial.print("MapHum= "); Serial.println(mapHumidityLev);
  snprintf(buf, sizeof(buf), "%d", HumidityLev);
  client.publish("irrigatore/stato/raw_humidity", buf);
  Serial.print("Humidity= "); Serial.println(HumidityLev);

  sensors.requestTemperatures();
  temperaturaC = sensors.getTempCByIndex(0);
  Serial.print("Temperatura: "); Serial.print(temperaturaC); Serial.println(" °C");
  Blynk.virtualWrite(V4, temperaturaC);
  char bufTemp[8];
  dtostrf(temperaturaC, 6, 2, bufTemp);
  client.publish("irrigatore/stato/temperatura", bufTemp);

  //printOLED();
}

void inviaStatoCorrente() {
  client.publish("irrigatore/comando/modalita", modalitaAutomatica ? "auto" : "manuale");
  client.publish("irrigatore/stato/pompa", pompaAttiva ? "on" : "off");

  char buf[8];
  snprintf(buf, sizeof(buf), "%d", HumidityLev);
  client.publish("irrigatore/stato/raw_humidity", buf);
  snprintf(buf, sizeof(buf), "%d", mapHumidityLev);
  client.publish("irrigatore/stato/map_humidity", buf);

  char bufTemp[8];
  dtostrf(temperaturaC, 6, 2, bufTemp);
  client.publish("irrigatore/stato/temperatura", bufTemp);
}

BLYNK_WRITE(V0) {
  if (!modalitaAutomatica) {
    int stato = param.asInt();
    if (stato == 1) attivaPompa();
    else disattivaPompa();
  }
}

BLYNK_WRITE(V3) {
  int selezione = param.asInt();
  modalitaAutomatica = (selezione == 1);
  if (modalitaAutomatica) {
    disattivaPompa();
    Blynk.virtualWrite(V0, 0);
  }
}

/*
void printOLED() {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.print("%Hum= "); display.print(mapHumidityLev);display.println("%");
  display.setTextSize(1);
  display.println();
  display.setTextSize(2);
  display.print("Hum= "); display.println(HumidityLev);
  display.setTextSize(1);
  display.println();
  display.setTextSize(2);
  display.print("T= "); display.println(temperaturaC, 1);
  display.display();
}*/

/**********************************************************************************/
void setup() {
  Serial.begin(115200);  
  pinMode(pinPompa, OUTPUT);
  digitalWrite(pinPompa, LOW);


  setup_wifi();
  
  sensors.begin();
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, password);

  modalitaAutomatica = false;
  Blynk.virtualWrite(V0, 0);
  Blynk.virtualWrite(V3, 0);
  timer.setInterval(6000L, inviaDati);
 
 /*
  Wire.begin(SDA_PIN, SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("Errore: Display SSD1306 non trovato!"));
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Avvio...");
  display.display();
  delay(1000);
*/

}

void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();
  Blynk.run();
  timer.run();

  // Pompa
  if (modalitaAutomatica) {
    if (!pompaAttiva && mapHumidityLev < MinHumidity && millis() - ultimoAvvioPompa > intervalloMinimo) {
      Serial.println("Attivo Pompa 1");
      attivaPompa();
      ultimoAvvioPompa = millis();
    }
    if (pompaAttiva && millis() - tempoAvvioPompa >= durataPompa  || mapHumidityLev > MinHumidity) {
      Serial.println("Spengo Pompa");
      disattivaPompa();
    }
  }

/*
  if (modalitaAutomatica && pompaAttiva && millis() - tempoAvvioPompa >= durataPompa && mapHumidityLev > MinHumidity) disattivaPompa();

  if (modalitaAutomatica && !pompaAttiva && mapHumidityLev < MinHumidity) attivaPompa();
*/
}
