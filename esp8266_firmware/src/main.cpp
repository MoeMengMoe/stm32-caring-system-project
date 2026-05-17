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
static const char *NODE_ID = "node01";

static const bool ENABLE_FAKE_DATA = false;
static const unsigned long FAKE_PUBLISH_INTERVAL_MS = 5000UL;
static const size_t UART_LINE_MAX_LEN = 64;

static WiFiClient wifi_client;
static PubSubClient mqtt_client(wifi_client);

static unsigned long last_publish_ms = 0;
static char uart_line[UART_LINE_MAX_LEN];
static size_t uart_line_len = 0;

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

static const char *risk_to_event(const int risk) {
  switch (risk) {
    case 0:
      return "normal";
    case 1:
      return "notice";
    case 2:
      return "warning";
    case 3:
      return "alarm";
    default:
      return "unknown";
  }
}

static bool publish_status_json(const float temperature,
                                const float humidity,
                                const int gas,
                                const int presence,
                                const int risk) {
  char payload[256];
  const int written = snprintf(
      payload,
      sizeof(payload),
      "{\"node_id\":\"%s\",\"temperature\":%.1f,\"humidity\":%.1f,"
      "\"gas\":%d,\"presence\":%d,\"risk\":%d,\"event\":\"%s\"}",
      NODE_ID,
      temperature,
      humidity,
      gas,
      presence,
      risk,
      risk_to_event(risk));

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[FAIL] MQTT payload format overflow");
    return false;
  }

  const bool ok = mqtt_client.publish(MQTT_TOPIC_STATUS, payload);
  Serial.print(ok ? "[INFO] Publish OK: " : "[WARN] Publish failed: ");
  Serial.println(payload);
  return ok;
}

static bool parse_csv_status(const char *line,
                             float *temperature,
                             float *humidity,
                             int *gas,
                             int *presence,
                             int *risk) {
  char extra = '\0';
  const int fields = sscanf(
      line,
      " %f , %f , %d , %d , %d %c",
      temperature,
      humidity,
      gas,
      presence,
      risk,
      &extra);

  if (fields != 5) {
    return false;
  }

  if (*presence < 0 || *presence > 1) {
    return false;
  }

  if (*risk < 0 || *risk > 3) {
    return false;
  }

  return true;
}

static void handle_uart_line(const char *line) {
  float temperature = 0.0F;
  float humidity = 0.0F;
  int gas = 0;
  int presence = 0;
  int risk = 0;

  if (!parse_csv_status(line, &temperature, &humidity, &gas, &presence, &risk)) {
    Serial.print("[WARN] Invalid UART status line: ");
    Serial.println(line);
    return;
  }

  publish_status_json(temperature, humidity, gas, presence, risk);
}

static void poll_uart_status(void) {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      if (uart_line_len > 0) {
        uart_line[uart_line_len] = '\0';
        handle_uart_line(uart_line);
        uart_line_len = 0;
      }
      continue;
    }

    if (uart_line_len + 1 >= sizeof(uart_line)) {
      Serial.println("[WARN] UART line overflow, drop current line");
      uart_line_len = 0;
      continue;
    }

    uart_line[uart_line_len++] = c;
  }
}

static void publish_fake_status(void) {
  publish_status_json(25.6F, 61.0F, 120, 1, 0);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("[INFO] ESP8266 MQTT UART gateway boot");
  Serial.println("[INFO] UART CSV format: temperature,humidity,gas,presence,risk");

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
  poll_uart_status();

  const unsigned long now = millis();
  if (ENABLE_FAKE_DATA && now - last_publish_ms >= FAKE_PUBLISH_INTERVAL_MS) {
    last_publish_ms = now;
    publish_fake_status();
  }
}
