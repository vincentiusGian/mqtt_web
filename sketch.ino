#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>

const int POMPA_PIN = 12;
const int SENSOR_PIN = 14;

const char* ssid = "Dapoer Iboe";
const char* password = "ga120281";
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 8883;
const char* mqtt_topic = "pompa/status";
const char* mqtt_cd = "pompa/countdown";
const char* mqtt_warning = "pompa/warning";
const char* mqtt_waiting = "pompa/waiting";  // 🆕 NEW: Topik untuk countdown waiting

bool hasSentClearedWarning = false;

WiFiClientSecure espClient;
PubSubClient client(espClient);

enum State { IDLE, ON30, OFF30, WAITING };
State currentState = IDLE;

unsigned long stateStartTime = 0;
bool sensor_active = false;

void callback(char* topic, byte* payload, unsigned int length) {
  // Tidak ada kontrol manual, jadi callback ini kosong
}

void reconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    WiFi.disconnect();
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("WiFi connected!");
  }

  if (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    if (client.connect("ESP8266Client-OnlySensor")) {
      Serial.println("MQTT connected!");
      // Tidak perlu subscribe topik karena tidak menerima perintah dari web
    } else {
      Serial.print("MQTT failed, rc=");
      Serial.println(client.state());
    }
  }
}

void publishCountdown(int seconds) {
  char payload[10];
  sprintf(payload, "%d", seconds);
  client.publish(mqtt_cd, payload);
}

void publishWaitingCountdown(int seconds) {
  char payload[10];
  sprintf(payload, "%d", seconds);
  client.publish(mqtt_waiting, payload);
}

void setup() {
  Serial.begin(9600);
  pinMode(POMPA_PIN, OUTPUT);
  pinMode(SENSOR_PIN, INPUT);
  digitalWrite(POMPA_PIN, HIGH); // Pompa mati awalnya

  WiFi.begin(ssid, password);
  espClient.setInsecure();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
}

void loop() {
  reconnect();
  client.loop();

  sensor_active = digitalRead(SENSOR_PIN) == HIGH;
  unsigned long now = millis();

  switch (currentState) {
    case IDLE:
      if (sensor_active) {
        currentState = ON30;
        stateStartTime = now;
        digitalWrite(POMPA_PIN, LOW);
        client.publish(mqtt_topic, "NYALA");
        Serial.println("Pompa ON dari IDLE");
      }
      break;

    case ON30:
      if (now - stateStartTime >= 1000) {
        int remaining = 30 - ((now - stateStartTime) / 1000);
        if (remaining >= 0 && remaining <= 30)
          publishCountdown(remaining);
      }
      if (now - stateStartTime >= 30000) {
        currentState = OFF30;
        stateStartTime = now;
        digitalWrite(POMPA_PIN, HIGH);
        client.publish(mqtt_topic, "MATI");
        Serial.println("Pompa OFF setelah 30s ON");
      }
      break;

    case OFF30:
      if (now - stateStartTime >= 30000) {
        if (sensor_active) {
          currentState = ON30;
          stateStartTime = now;
          digitalWrite(POMPA_PIN, LOW);
          client.publish(mqtt_topic, "NYALA");
          Serial.println("Pompa ON lagi");
        } else {
          currentState = WAITING;
          stateStartTime = now;
          client.publish(mqtt_warning, "Water supply is depleted. Refilling is required. System will retry in 5 minutes.");
          Serial.println("Air habis, tunggu 5 menit...");
        }
      }
      break;

    case WAITING: {
  unsigned long elapsed = now - stateStartTime;
  int remaining = (300000 - elapsed) / 1000;

  if (remaining >= 0 && remaining <= 300) {
    publishWaitingCountdown(remaining);
  }

  // ✅ Kirim sinyal penghapusan warning hanya sekali
  if (sensor_active && !hasSentClearedWarning) {
    client.publish(mqtt_warning, "Water supply has been restored. Resuming system operations.");  // bisa pakai "cleared" biar tidak kosong
    hasSentClearedWarning = true;
    Serial.println("Air terdeteksi, warning dihapus");
  }

  if (elapsed >= 300000) {
    hasSentClearedWarning = false;  // reset flag

    if (sensor_active) {
      currentState = ON30;
      stateStartTime = now;
      digitalWrite(POMPA_PIN, LOW);
      client.publish(mqtt_topic, "NYALA");
      Serial.println("5 menit selesai & air tersedia → Pompa ON");
    } else {
      currentState = IDLE;
      Serial.println("5 menit selesai tapi air masih habis → IDLE");
    }
  }

  break;
}
  }

  delay(10); // ringan
}
