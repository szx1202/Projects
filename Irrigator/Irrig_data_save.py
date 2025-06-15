import paho.mqtt.client as mqtt
from datetime import datetime
import os

# Configura il broker MQTT
broker = "192.168.1.104"
port = 1883
username = "pi"
password = "$oklaM01"

# File di log
file_path = "/var/www/html/Irrigatore/datalog.csv"

# Dati da memorizzare temporaneamente
dati = {
    "temperatura": None,
    "umidita": None,
    "maphumidity": None
}

# Scrive header se il file è nuovo o vuoto
def inizializza_file():
    if not os.path.exists(file_path) or os.path.getsize(file_path) == 0:
        with open(file_path, "w") as f:
            f.write("Date,Time,Temp,Hum,Hum %\n")

def salva_su_file():
    if all(value is not None for value in dati.values()):
        now = datetime.now()
        data = now.strftime('%Y-%m-%d')
        ora = now.strftime('%H:%M')
        riga = f"{data},{ora},{dati['temperatura']},{dati['umidita']},{dati['maphumidity']}\n"
        with open(file_path, "a") as f:
            f.write(riga)
        print(f"[SALVATO] {riga.strip()}")

def on_connect(client, userdata, flags, rc):
    print("Connesso al broker con codice", rc)
    client.subscribe("irrigatore/stato/temperatura")
    client.subscribe("irrigatore/stato/raw_humidity")
    client.subscribe("irrigatore/stato/map_humidity")

def on_message(client, userdata, msg):
    topic = msg.topic
    payload = msg.payload.decode()

    if topic.endswith("temperatura"):
        dati["temperatura"] = payload
    elif topic.endswith("raw_humidity"):
        dati["umidita"] = payload
    elif topic.endswith("map_humidity"):
        dati["maphumidity"] = payload

# Inizializza file con header se necessario
inizializza_file()

client = mqtt.Client()
client.username_pw_set(username, password)
client.on_connect = on_connect
client.on_message = on_message

client.connect(broker, port, 60)
client.loop_start()

# Richiede i dati una volta all'ora
import time
while True:
    client.publish("irrigatore/get/stato", "")
    time.sleep(10)  # aspetta un po' che i dati arrivino
    salva_su_file()
    time.sleep(1800)  # aspetta 30 min
