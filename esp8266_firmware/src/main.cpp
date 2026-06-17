#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>

// TODO(Simon): Replace these values before uploading.
static const char *WIFI_SSID = "Cudy-E5A6";
static const char *WIFI_PASSWORD = "405405405";
static const char *MQTT_HOST = "192.168.10.249";
static const uint16_t MQTT_PORT = 1883;

static const char *NODE_ID = "node01";

static const bool ENABLE_FAKE_DATA = false;
static const unsigned long FAKE_PUBLISH_INTERVAL_MS = 5000UL;
static const size_t UART_LINE_MAX_LEN = 192;
static const size_t MQTT_PAYLOAD_MAX_LEN = 384;

static WiFiClient wifi_client;
static PubSubClient mqtt_client(wifi_client);

static char mqtt_client_id[32];
static char mqtt_topic_status[64];
static char mqtt_topic_event[64];
static char mqtt_topic_demo_command[64];
static char mqtt_topic_demo_state[64];
static char mqtt_topic_relay_set[4][64];
static char mqtt_topic_relay_state[4][64];
static char mqtt_topic_relay_result[4][64];

static unsigned long last_publish_ms = 0;
static uint32_t fake_seq = 0;
static uint32_t last_uart_seq = 0;
static uint32_t next_gateway_request_id = 1;
static bool has_last_uart_seq = false;
static char uart_line[UART_LINE_MAX_LEN];
static size_t uart_line_len = 0;

static void build_mqtt_names(void) {
  snprintf(mqtt_client_id, sizeof(mqtt_client_id), "eldercare-%s", NODE_ID);
  snprintf(mqtt_topic_status, sizeof(mqtt_topic_status), "eldercare/%s/status", NODE_ID);
  snprintf(mqtt_topic_event, sizeof(mqtt_topic_event), "eldercare/%s/event", NODE_ID);
  snprintf(mqtt_topic_demo_command,
           sizeof(mqtt_topic_demo_command),
           "eldercare/%s/demo/command",
           NODE_ID);
  snprintf(mqtt_topic_demo_state, sizeof(mqtt_topic_demo_state), "eldercare/%s/demo/state", NODE_ID);

  for (uint8_t relay = 1; relay <= 4; relay++) {
    snprintf(mqtt_topic_relay_set[relay - 1],
             sizeof(mqtt_topic_relay_set[relay - 1]),
             "eldercare/%s/relay/%u/set",
             NODE_ID,
             relay);
    snprintf(mqtt_topic_relay_state[relay - 1],
             sizeof(mqtt_topic_relay_state[relay - 1]),
             "eldercare/%s/relay/%u/state",
             NODE_ID,
             relay);
    snprintf(mqtt_topic_relay_result[relay - 1],
             sizeof(mqtt_topic_relay_result[relay - 1]),
             "eldercare/%s/relay/%u/result",
             NODE_ID,
             relay);
  }
}

static const char *scenario_to_text(const int scenario) {
  switch (scenario) {
    case 0:
      return "NONE";
    case 1:
      return "SOS_OR_FALL_SIM";
    case 2:
      return "LONG_STILL_NO_RESPONSE";
    case 3:
      return "OFFLINE_AUTONOMY";
    default:
      return "UNKNOWN";
  }
}

static const char *event_type_to_text(const int event_type) {
  switch (event_type) {
    case 0:
      return "STATUS_ONLY";
    case 1:
      return "REMOTE_TRIGGER";
    case 2:
      return "SOS_BUTTON";
    case 3:
      return "LONG_STILL";
    case 4:
      return "USER_ACK";
    case 5:
      return "ACK_TIMEOUT";
    case 6:
      return "CLEAR_ALARM";
    case 7:
      return "NETWORK_LOST";
    case 8:
      return "NETWORK_RESTORED";
    case 9:
      return "POWER_BACKUP_ENTER";
    case 10:
      return "POWER_NORMAL_RESTORED";
    default:
      return "UNKNOWN";
  }
}

static const char *trigger_source_to_text(const int trigger_source) {
  switch (trigger_source) {
    case 0:
      return "LOCAL";
    case 1:
      return "REMOTE";
    case 2:
      return "BUTTON";
    case 3:
      return "RADAR";
    case 4:
      return "NETWORK";
    case 5:
      return "POWER";
    default:
      return "UNKNOWN";
  }
}

static const char *app_state_to_text(const int state) {
  switch (state) {
    case 0:
      return "NORMAL";
    case 1:
      return "NOTICE";
    case 2:
      return "ACK_WAIT";
    case 3:
      return "ALARM";
    case 4:
      return "NO_RESPONSE";
    case 5:
      return "CLEARED";
    default:
      return "UNKNOWN";
  }
}

static const char *event_result_to_text(const int result) {
  switch (result) {
    case 0:
      return "CREATED";
    case 1:
      return "WAITING_ACK";
    case 2:
      return "ACKNOWLEDGED";
    case 3:
      return "ESCALATED";
    case 4:
      return "CLEARED";
    case 5:
      return "OFFLINE_CACHED";
    case 6:
      return "BACKFILLED";
    case 7:
      return "FAILED";
    default:
      return "UNKNOWN";
  }
}

static const char *network_state_to_text(const int network_state) {
  switch (network_state) {
    case 0:
      return "ONLINE";
    case 1:
      return "OFFLINE";
    case 2:
      return "RESTORED";
    default:
      return "UNKNOWN";
  }
}

static const char *power_state_to_text(const int power_state) {
  switch (power_state) {
    case 0:
      return "NORMAL";
    case 1:
      return "BACKUP";
    case 2:
      return "LOW";
    default:
      return "UNKNOWN";
  }
}

static bool is_valid_enum_text(const char *value) {
  return value != nullptr && strcmp(value, "UNKNOWN") != 0;
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

static const char *mask_state_text(const uint8_t relay_state_mask, const uint8_t relay_id) {
  const uint8_t bit = static_cast<uint8_t>(1U << (relay_id - 1U));
  return ((relay_state_mask & bit) != 0U) ? "ON" : "OFF";
}

static void publish_relay_state_from_mask(const uint8_t relay_id,
                                          const uint8_t relay_state_mask) {
  char payload[64];
  const char *state = mask_state_text(relay_state_mask, relay_id);
  const int written = snprintf(payload,
                               sizeof(payload),
                               "{\"node_id\":\"%s\",\"relay_id\":%u,\"state\":\"%s\","
                               "\"request_id\":null}",
                               NODE_ID,
                               relay_id,
                               state);

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[FAIL] relay state payload overflow");
    return;
  }

  mqtt_client.publish(mqtt_topic_relay_state[relay_id - 1U], payload, true);
}

static bool parse_event_frame(const char *line,
                              uint32_t *event_id,
                              int *scenario,
                              int *event_type,
                              int *trigger_source,
                              int *state_before,
                              int *state_after,
                              int *risk,
                              int *result,
                              int *network_state,
                              int *power_state,
                              uint32_t *flags,
                              uint32_t *timestamp_ms) {
  char frame_type = '\0';
  char extra = '\0';
  unsigned long parsed_event_id = 0;
  unsigned long parsed_flags = 0;
  unsigned long parsed_timestamp_ms = 0;

  const int fields = sscanf(
      line,
      " %c , %lu , %d , %d , %d , %d , %d , %d , %d , %d , %d , %lu , %lu %c",
      &frame_type,
      &parsed_event_id,
      scenario,
      event_type,
      trigger_source,
      state_before,
      state_after,
      risk,
      result,
      network_state,
      power_state,
      &parsed_flags,
      &parsed_timestamp_ms,
      &extra);

  if (fields != 13 || frame_type != 'E') {
    return false;
  }

  if (!is_valid_enum_text(scenario_to_text(*scenario)) ||
      !is_valid_enum_text(event_type_to_text(*event_type)) ||
      !is_valid_enum_text(trigger_source_to_text(*trigger_source)) ||
      !is_valid_enum_text(app_state_to_text(*state_before)) ||
      !is_valid_enum_text(app_state_to_text(*state_after)) ||
      *risk < 0 || *risk > 3 ||
      !is_valid_enum_text(event_result_to_text(*result)) ||
      !is_valid_enum_text(network_state_to_text(*network_state)) ||
      !is_valid_enum_text(power_state_to_text(*power_state))) {
    return false;
  }

  *event_id = static_cast<uint32_t>(parsed_event_id);
  *flags = static_cast<uint32_t>(parsed_flags);
  *timestamp_ms = static_cast<uint32_t>(parsed_timestamp_ms);
  return true;
}

static bool publish_event_json(const uint32_t event_id,
                               const int scenario,
                               const int event_type,
                               const int trigger_source,
                               const int state_before,
                               const int state_after,
                               const int risk,
                               const int result,
                               const int network_state,
                               const int power_state,
                               const uint32_t flags,
                               const uint32_t timestamp_ms) {
  char payload[MQTT_PAYLOAD_MAX_LEN];
  const int written = snprintf(
      payload,
      sizeof(payload),
      "{\"node_id\":\"%s\",\"event_id\":%lu,\"scenario\":\"%s\","
      "\"event_type\":\"%s\",\"trigger_source\":\"%s\",\"state_before\":\"%s\","
      "\"state_after\":\"%s\",\"risk\":%d,\"result\":\"%s\","
      "\"network_state\":\"%s\",\"power_state\":\"%s\",\"flags\":%lu,"
      "\"timestamp_ms\":%lu}",
      NODE_ID,
      static_cast<unsigned long>(event_id),
      scenario_to_text(scenario),
      event_type_to_text(event_type),
      trigger_source_to_text(trigger_source),
      app_state_to_text(state_before),
      app_state_to_text(state_after),
      risk,
      event_result_to_text(result),
      network_state_to_text(network_state),
      power_state_to_text(power_state),
      static_cast<unsigned long>(flags),
      static_cast<unsigned long>(timestamp_ms));

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[FAIL] MQTT event payload overflow");
    return false;
  }

  const bool ok = mqtt_client.publish(mqtt_topic_event, payload);
  Serial.print(ok ? "[INFO] Publish event OK: " : "[WARN] Publish event failed: ");
  Serial.println(payload);
  return ok;
}

static bool publish_status_json(const uint32_t seq,
                                const float temperature,
                                const float humidity,
                                const int gas,
                                const int presence,
                                const int risk,
                                const uint8_t relay_state_mask,
                                const uint8_t cloud_perm_mask) {
  char payload[MQTT_PAYLOAD_MAX_LEN];
  const int written = snprintf(
      payload,
      sizeof(payload),
      "{\"node_id\":\"%s\",\"seq\":%lu,\"temperature\":%.1f,\"humidity\":%.1f,"
      "\"gas\":%d,\"presence\":%d,\"risk\":%d,\"event\":\"%s\","
      "\"relay_state_mask\":%u,\"cloud_perm_mask\":%u}",
      NODE_ID,
      static_cast<unsigned long>(seq),
      temperature,
      humidity,
      gas,
      presence,
      risk,
      risk_to_event(risk),
      relay_state_mask,
      cloud_perm_mask);

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[FAIL] MQTT status payload overflow");
    return false;
  }

  const bool ok = mqtt_client.publish(mqtt_topic_status, payload);
  Serial.print(ok ? "[INFO] Publish status OK: " : "[WARN] Publish status failed: ");
  Serial.println(payload);

  if (ok) {
    for (uint8_t relay = 1; relay <= 4; relay++) {
      publish_relay_state_from_mask(relay, relay_state_mask);
    }
  }

  return ok;
}

static bool parse_status_frame(const char *line,
                               uint32_t *seq,
                               float *temperature,
                               float *humidity,
                               int *gas,
                               int *presence,
                               int *risk,
                               uint8_t *relay_state_mask,
                               uint8_t *cloud_perm_mask) {
  char frame_type = '\0';
  char extra = '\0';
  unsigned long parsed_seq = 0;
  unsigned int parsed_relay_state_mask = 0;
  unsigned int parsed_cloud_perm_mask = 0;

  const int fields = sscanf(
      line,
      " %c , %lu , %f , %f , %d , %d , %d , %u , %u %c",
      &frame_type,
      &parsed_seq,
      temperature,
      humidity,
      gas,
      presence,
      risk,
      &parsed_relay_state_mask,
      &parsed_cloud_perm_mask,
      &extra);

  if (fields != 9 || frame_type != 'S') {
    return false;
  }

  if (*presence < 0 || *presence > 1 || *risk < 0 || *risk > 3 ||
      parsed_relay_state_mask > 15U || parsed_cloud_perm_mask > 15U) {
    return false;
  }

  *seq = static_cast<uint32_t>(parsed_seq);
  *relay_state_mask = static_cast<uint8_t>(parsed_relay_state_mask);
  *cloud_perm_mask = static_cast<uint8_t>(parsed_cloud_perm_mask);
  return true;
}

static bool parse_relay_result_frame(const char *line,
                                     uint32_t *request_id,
                                     uint8_t *relay_id,
                                     char *result,
                                     size_t result_size,
                                     char *state,
                                     size_t state_size,
                                     char *reason,
                                     size_t reason_size) {
  char frame_type = '\0';
  char extra = '\0';
  unsigned long parsed_request_id = 0;
  unsigned int parsed_relay_id = 0;
  char parsed_result[8] = {0};
  char parsed_state[8] = {0};
  char parsed_reason[24] = {0};

  const int fields = sscanf(line,
                            " %c , %lu , %u , %7[A-Z] , %7[A-Z] , %23[a-z_] %c",
                            &frame_type,
                            &parsed_request_id,
                            &parsed_relay_id,
                            parsed_result,
                            parsed_state,
                            parsed_reason,
                            &extra);

  if (fields != 6 || frame_type != 'R' || parsed_relay_id < 1U || parsed_relay_id > 4U) {
    return false;
  }

  if (strcmp(parsed_state, "ON") != 0 && strcmp(parsed_state, "OFF") != 0) {
    return false;
  }

  if (strcmp(parsed_result, "OK") != 0 && strcmp(parsed_result, "DENY") != 0 &&
      strcmp(parsed_result, "ERR") != 0) {
    return false;
  }

  *request_id = static_cast<uint32_t>(parsed_request_id);
  *relay_id = static_cast<uint8_t>(parsed_relay_id);

  strncpy(result, parsed_result, result_size - 1U);
  result[result_size - 1U] = '\0';
  strncpy(state, parsed_state, state_size - 1U);
  state[state_size - 1U] = '\0';
  strncpy(reason, parsed_reason, reason_size - 1U);
  reason[reason_size - 1U] = '\0';
  return true;
}

static bool publish_relay_result_json(const uint32_t request_id,
                                      const uint8_t relay_id,
                                      const char *result,
                                      const char *state,
                                      const char *reason) {
  char payload[128];
  const int written = snprintf(payload,
                               sizeof(payload),
                               "{\"node_id\":\"%s\",\"request_id\":%lu,\"relay_id\":%u,"
                               "\"result\":\"%s\",\"state\":\"%s\",\"reason\":\"%s\"}",
                               NODE_ID,
                               static_cast<unsigned long>(request_id),
                               relay_id,
                               result,
                               state,
                               reason);

  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[FAIL] relay result payload overflow");
    return false;
  }

  const bool result_ok = mqtt_client.publish(mqtt_topic_relay_result[relay_id - 1U], payload);

  char state_payload[64];
  const int state_written = snprintf(state_payload,
                                     sizeof(state_payload),
                                     "{\"node_id\":\"%s\",\"relay_id\":%u,\"state\":\"%s\","
                                     "\"request_id\":%lu}",
                                     NODE_ID,
                                     relay_id,
                                     state,
                                     static_cast<unsigned long>(request_id));
  bool state_ok = false;
  if (state_written > 0 && state_written < static_cast<int>(sizeof(state_payload))) {
    state_ok = mqtt_client.publish(mqtt_topic_relay_state[relay_id - 1U], state_payload, true);
  }

  Serial.print((result_ok && state_ok) ? "[INFO] Publish relay result OK: "
                                      : "[WARN] Publish relay result failed: ");
  Serial.println(payload);
  return result_ok && state_ok;
}

static void handle_uart_line(const char *line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }

  if (line[0] == 'S') {
    uint32_t seq = 0;
    float temperature = 0.0F;
    float humidity = 0.0F;
    int gas = 0;
    int presence = 0;
    int risk = 0;
    uint8_t relay_state_mask = 0;
    uint8_t cloud_perm_mask = 15;

    if (!parse_status_frame(line,
                            &seq,
                            &temperature,
                            &humidity,
                            &gas,
                            &presence,
                            &risk,
                            &relay_state_mask,
                            &cloud_perm_mask)) {
      Serial.print("[WARN] Invalid UART status frame: ");
      Serial.println(line);
      return;
    }

    if (has_last_uart_seq && seq <= last_uart_seq) {
      Serial.print("[WARN] UART status seq not increasing, seq=");
      Serial.println(seq);
    }
    last_uart_seq = seq;
    has_last_uart_seq = true;

    publish_status_json(seq,
                        temperature,
                        humidity,
                        gas,
                        presence,
                        risk,
                        relay_state_mask,
                        cloud_perm_mask);
    return;
  }

  if (line[0] == 'R') {
    uint32_t request_id = 0;
    uint8_t relay_id = 0;
    char result[8] = {0};
    char state[8] = {0};
    char reason[24] = {0};

    if (!parse_relay_result_frame(line,
                                  &request_id,
                                  &relay_id,
                                  result,
                                  sizeof(result),
                                  state,
                                  sizeof(state),
                                  reason,
                                  sizeof(reason))) {
      Serial.print("[WARN] Invalid UART relay result frame: ");
      Serial.println(line);
      return;
    }

    publish_relay_result_json(request_id, relay_id, result, state, reason);
    return;
  }

  if (line[0] == 'E') {
    uint32_t event_id = 0;
    int scenario = 0;
    int event_type = 0;
    int trigger_source = 0;
    int state_before = 0;
    int state_after = 0;
    int risk = 0;
    int result = 0;
    int network_state = 0;
    int power_state = 0;
    uint32_t flags = 0;
    uint32_t timestamp_ms = 0;

    if (!parse_event_frame(line,
                           &event_id,
                           &scenario,
                           &event_type,
                           &trigger_source,
                           &state_before,
                           &state_after,
                           &risk,
                           &result,
                           &network_state,
                           &power_state,
                           &flags,
                           &timestamp_ms)) {
      Serial.print("[WARN] Invalid UART event frame: ");
      Serial.println(line);
      return;
    }

    publish_event_json(event_id,
                       scenario,
                       event_type,
                       trigger_source,
                       state_before,
                       state_after,
                       risk,
                       result,
                       network_state,
                       power_state,
                       flags,
                       timestamp_ms);
    return;
  }

  Serial.print("[WARN] Unsupported UART frame: ");
  Serial.println(line);
}

static void poll_uart_frames(void) {
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
  publish_status_json(fake_seq++, 25.6F, 61.0F, 120, 1, 0, 0U, 15U);
}

static bool topic_to_relay_id(const char *topic, uint8_t *relay_id) {
  if (topic == nullptr || relay_id == nullptr) {
    return false;
  }

  for (uint8_t relay = 1; relay <= 4; relay++) {
    if (strcmp(topic, mqtt_topic_relay_set[relay - 1U]) == 0) {
      *relay_id = relay;
      return true;
    }
  }

  return false;
}

static int command_type_to_code(const char *command_type) {
  if (strcmp(command_type, "TRIGGER_SCENARIO") == 0) {
    return 1;
  }
  if (strcmp(command_type, "USER_ACK") == 0) {
    return 2;
  }
  if (strcmp(command_type, "CLEAR_ALARM") == 0) {
    return 3;
  }
  if (strcmp(command_type, "SIMULATE_NETWORK") == 0) {
    return 4;
  }
  if (strcmp(command_type, "SET_RELAY") == 0) {
    return 5;
  }
  return -1;
}

static int scenario_to_code(const char *scenario) {
  if (strcmp(scenario, "NONE") == 0) {
    return 0;
  }
  if (strcmp(scenario, "SOS_OR_FALL_SIM") == 0) {
    return 1;
  }
  if (strcmp(scenario, "LONG_STILL_NO_RESPONSE") == 0) {
    return 2;
  }
  if (strcmp(scenario, "OFFLINE_AUTONOMY") == 0) {
    return 3;
  }
  return -1;
}

static bool extract_json_string_value(const char *payload,
                                      const char *key,
                                      char *out,
                                      size_t out_size) {
  if (payload == nullptr || key == nullptr || out == nullptr || out_size == 0U) {
    return false;
  }

  char pattern[32];
  snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  const char *key_pos = strstr(payload, pattern);
  if (key_pos == nullptr) {
    return false;
  }

  const char *colon = strchr(key_pos + strlen(pattern), ':');
  if (colon == nullptr) {
    return false;
  }

  const char *value = colon + 1;
  while (*value == ' ' || *value == '\t') {
    value++;
  }

  if (*value == '"') {
    value++;
    const char *end = strchr(value, '"');
    if (end == nullptr) {
      return false;
    }
    const size_t len = static_cast<size_t>(end - value);
    if (len == 0U || len >= out_size) {
      return false;
    }
    memcpy(out, value, len);
    out[len] = '\0';
    return true;
  }

  size_t len = 0;
  while (value[len] != '\0' && value[len] != ',' && value[len] != '}' &&
         value[len] != ' ' && value[len] != '\t') {
    len++;
  }
  if (len == 0U || len >= out_size) {
    return false;
  }
  memcpy(out, value, len);
  out[len] = '\0';
  return true;
}

static bool parse_relay_set_payload(const char *payload,
                                    char *request_id,
                                    size_t request_id_size,
                                    char *action,
                                    size_t action_size) {
  if (payload == nullptr || request_id == nullptr || action == nullptr ||
      request_id_size == 0U || action_size == 0U) {
    return false;
  }

  request_id[0] = '\0';
  action[0] = '\0';

  if (strcmp(payload, "ON") == 0 || strcmp(payload, "OFF") == 0) {
    snprintf(request_id, request_id_size, "%lu", static_cast<unsigned long>(next_gateway_request_id++));
    strncpy(action, payload, action_size - 1U);
    action[action_size - 1U] = '\0';
    return true;
  }

  if (!extract_json_string_value(payload, "action", action, action_size)) {
    return false;
  }

  if (strcmp(action, "ON") != 0 && strcmp(action, "OFF") != 0) {
    return false;
  }

  if (!extract_json_string_value(payload, "request_id", request_id, request_id_size)) {
    snprintf(request_id, request_id_size, "%lu", static_cast<unsigned long>(next_gateway_request_id++));
  }

  for (size_t i = 0; request_id[i] != '\0'; i++) {
    if (request_id[i] < '0' || request_id[i] > '9') {
      snprintf(request_id, request_id_size, "%lu", static_cast<unsigned long>(next_gateway_request_id++));
      break;
    }
  }

  return true;
}

static bool parse_demo_command_payload(const char *payload,
                                       char *request_id,
                                       size_t request_id_size,
                                       int *command_type,
                                       int *scenario,
                                       int *value) {
  if (payload == nullptr || request_id == nullptr || command_type == nullptr ||
      scenario == nullptr || value == nullptr || request_id_size == 0U) {
    return false;
  }

  char command_type_text[24];
  char scenario_text[32];
  char value_text[12];

  if (!extract_json_string_value(payload, "command_type", command_type_text, sizeof(command_type_text))) {
    return false;
  }
  if (!extract_json_string_value(payload, "scenario", scenario_text, sizeof(scenario_text))) {
    strncpy(scenario_text, "NONE", sizeof(scenario_text) - 1U);
    scenario_text[sizeof(scenario_text) - 1U] = '\0';
  }
  if (!extract_json_string_value(payload, "value", value_text, sizeof(value_text))) {
    strncpy(value_text, "1", sizeof(value_text) - 1U);
    value_text[sizeof(value_text) - 1U] = '\0';
  }

  *command_type = command_type_to_code(command_type_text);
  *scenario = scenario_to_code(scenario_text);
  *value = atoi(value_text);
  if (*command_type < 0 || *scenario < 0) {
    return false;
  }

  if (!extract_json_string_value(payload, "request_id", request_id, request_id_size)) {
    snprintf(request_id, request_id_size, "%lu", static_cast<unsigned long>(next_gateway_request_id++));
  }

  for (size_t i = 0; request_id[i] != '\0'; i++) {
    if (request_id[i] < '0' || request_id[i] > '9') {
      snprintf(request_id, request_id_size, "%lu", static_cast<unsigned long>(next_gateway_request_id++));
      break;
    }
  }

  return true;
}

static void forward_relay_command_to_stm32(const uint8_t relay_id,
                                           const char *request_id,
                                           const char *action) {
  char frame[48];
  const int written = snprintf(frame, sizeof(frame), "C,%s,%u,%s\r\n", request_id, relay_id, action);
  if (written <= 0 || written >= static_cast<int>(sizeof(frame))) {
    Serial.println("[FAIL] relay command frame overflow");
    return;
  }

  Serial.print(frame);
}

static void forward_demo_command_to_stm32(const char *request_id,
                                          const int command_type,
                                          const int scenario,
                                          const int value) {
  char frame[48];
  const int written = snprintf(frame,
                               sizeof(frame),
                               "D,%s,%d,%d,%d\r\n",
                               request_id,
                               command_type,
                               scenario,
                               value);
  if (written <= 0 || written >= static_cast<int>(sizeof(frame))) {
    Serial.println("[FAIL] demo command frame overflow");
    return;
  }

  Serial.print(frame);
}

static void publish_demo_state(const char *request_id,
                               const int command_type,
                               const int scenario,
                               const int value,
                               const char *result) {
  char payload[160];
  const int written = snprintf(payload,
                               sizeof(payload),
                               "{\"node_id\":\"%s\",\"request_id\":%s,"
                               "\"command_type\":%d,\"scenario\":%d,"
                               "\"value\":%d,\"result\":\"%s\"}",
                               NODE_ID,
                               request_id,
                               command_type,
                               scenario,
                               value,
                               result);
  if (written <= 0 || written >= static_cast<int>(sizeof(payload))) {
    Serial.println("[FAIL] demo state payload overflow");
    return;
  }

  mqtt_client.publish(mqtt_topic_demo_state, payload, false);
}

static void on_mqtt_message(char *topic, byte *payload, unsigned int length) {
  uint8_t relay_id = 0;
  if (length + 1U > MQTT_PAYLOAD_MAX_LEN) {
    Serial.println("[WARN] MQTT payload too long");
    return;
  }

  char text[MQTT_PAYLOAD_MAX_LEN];
  memcpy(text, payload, length);
  text[length] = '\0';

  if (strcmp(topic, mqtt_topic_demo_command) == 0) {
    char request_id[16];
    int command_type = 0;
    int scenario = 0;
    int value = 0;

    if (!parse_demo_command_payload(text,
                                    request_id,
                                    sizeof(request_id),
                                    &command_type,
                                    &scenario,
                                    &value)) {
      Serial.print("[WARN] Invalid demo command payload: ");
      Serial.println(text);
      return;
    }

    forward_demo_command_to_stm32(request_id, command_type, scenario, value);
    publish_demo_state(request_id, command_type, scenario, value, "FORWARDED");
    return;
  }

  if (!topic_to_relay_id(topic, &relay_id)) {
    return;
  }

  char request_id[16];
  char action[8];
  if (!parse_relay_set_payload(text, request_id, sizeof(request_id), action, sizeof(action))) {
    Serial.print("[WARN] Invalid relay set payload: ");
    Serial.println(text);
    return;
  }

  forward_relay_command_to_stm32(relay_id, request_id, action);
}

static void subscribe_relay_topics(void) {
  for (uint8_t relay = 1; relay <= 4; relay++) {
    const bool ok = mqtt_client.subscribe(mqtt_topic_relay_set[relay - 1U]);
    Serial.print(ok ? "[INFO] Subscribed: " : "[WARN] Subscribe failed: ");
    Serial.println(mqtt_topic_relay_set[relay - 1U]);
  }
}

static void subscribe_gateway_topics(void) {
  subscribe_relay_topics();

  const bool ok = mqtt_client.subscribe(mqtt_topic_demo_command);
  Serial.print(ok ? "[INFO] Subscribed: " : "[WARN] Subscribe failed: ");
  Serial.println(mqtt_topic_demo_command);
}

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
  mqtt_client.setCallback(on_mqtt_message);

  while (!mqtt_client.connected()) {
    Serial.print("[INFO] Connecting MQTT: ");
    Serial.print(MQTT_HOST);
    Serial.print(":");
    Serial.println(MQTT_PORT);

    if (mqtt_client.connect(mqtt_client_id)) {
      Serial.println("[INFO] MQTT connected");
      subscribe_gateway_topics();
      return;
    }

    Serial.print("[WARN] MQTT connect failed, state=");
    Serial.println(mqtt_client.state());
    delay(1000);
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  build_mqtt_names();

  Serial.println();
  Serial.println("[INFO] ESP8266 MQTT UART gateway boot");
  Serial.println("[INFO] UART CSV formats: S,status E,event R,relay-result");
  Serial.println("[INFO] MQTT relay set and demo commands are forwarded as C/D frames");

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
  poll_uart_frames();

  const unsigned long now = millis();
  if (ENABLE_FAKE_DATA && now - last_publish_ms >= FAKE_PUBLISH_INTERVAL_MS) {
    last_publish_ms = now;
    publish_fake_status();
  }
}
