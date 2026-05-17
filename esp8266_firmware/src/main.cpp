#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// TODO(Simon): Replace these values before uploading.
static const char *WIFI_SSID = "Xiaomi_CC47";
static const char *WIFI_PASSWORD = "405405405";
static const char *MQTT_HOST = "192.168.31.100";
static const uint16_t MQTT_PORT = 1883;

static const char *MQTT_CLIENT_ID = "eldercare-node01";
static const char *MQTT_TOPIC_STATUS = "eldercare/node01/status";

static WiFiClient wifi_client;
static PubSubClient mqtt_client(wifi_client);

static unsigned long last_publish_ms = 0;
static uint32_t seq = 0;

static void connect_wifi(void) {
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  Serial.print("[INFO] Connecting WiFi: ");
  Serial.println(WIFI_SSID);

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("[INFO] WiFi connected, IP: ");
  Serial.println(WiFi.localIP());
}

static void connect_mqtt(void) {
  mqtt_client.setServer(MQTT_HOST, MQTT_PORT);

  while (!mqtt_client.connected()) {
    Serial.print("[INFO] Connecting MQTT: ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    if (mqtt_client.connect(MQTT_CLIENT_ID)) {
      Serial.println("[INFO] MQTT connected");
      return;
    }

    Serial.print("[WARN] MQTT connect failed, state=");
    Serial.println(mqtt_client.state());
    delay(1000);
  }
}

static void publish_fake_status(void) {
  char payload[256];
  const int written = snprintf(
      payload,
      sizeof(payload),
      "{\"node_id\":\"node01\",\"seq\":%lu,\"temperature\":25.6,"
      "\"humidity\":61,\"gas\":120,\"presence\":1,\"risk\":0,"
      "\"event\":\"normal\"}",
      static_cast<unsigned long>(seq++));

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[FAIL] MQTT payload format overflow");
    return;
  }

  const bool ok = mqtt_client.publish(MQTT_TOPIC_STATUS, payload);
  Serial.print(ok ? "[INFO] Publish OK: " : "[WARN] Publish failed: ");
  Serial.println(payload);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("[INFO] ESP8266 MQTT fake-data gateway boot");

  connect_wifi();
  connect_mqtt();
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connect_wifi();
  }

  if (!mqtt_client.connected()) {
    connect_mqtt();
  }

  mqtt_client.loop();

  const unsigned long now = millis();
  if (now - last_publish_ms >= 5000UL) {
    last_publish_ms = now;
    publish_fake_status();
  }
}
