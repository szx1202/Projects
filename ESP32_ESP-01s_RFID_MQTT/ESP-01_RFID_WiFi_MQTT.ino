/* questo programma stabilisce una connessione alla rete e sottoscrive la coda sensor/rfid da dove legge il messaggio inviato al broker dal lettore rfid
 *  1= card riconosciuta
 *  0= card conosciuta.
 *  in caso di 1 attiva il relay per accendere un led
 */

#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// Update these with values suitable for your network.

const char* ssid = "LZ_24G"; 
const char* password = "*andromedA01.";
const char* mqtt_server = "192.168.1.104";
const char* mqtt_topic = "sensor/rfid";
const int mqtt_port = 1883;

const int pinRelay= 0;

WiFiClient espClient;
PubSubClient client(espClient);

void setup_wifi() {
  delay(10);
  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
}

void callback(char* topic, byte* payload, unsigned int length) {
  String message = "";
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message received: ");
  Serial.println(message);

  if (message == "1") {
    digitalWrite(pinRelay, LOW);
  } 
  else if (message == "2") 
  {
    digitalWrite(pinRelay, HIGH);
  } 
  else {
    Serial.println("Invalid message");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("ESP01S_Client")) {
      Serial.println("connected");
      client.subscribe(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" retrying in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(9600);
  
  pinMode(pinRelay, OUTPUT);
  digitalWrite(pinRelay, LOW);
  delay(3000);
  digitalWrite(pinRelay, HIGH);
  
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
}
