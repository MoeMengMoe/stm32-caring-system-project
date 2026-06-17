#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${CONTAINER:-eldercare-mosquitto}"
TOPIC="${TOPIC:-eldercare/node01/event}"
DELAY_SECONDS="${DELAY_SECONDS:-2}"

samples=(
  '{"node_id":"node01","event_id":1001,"scenario":"SOS_OR_FALL_SIM","event_type":"REMOTE_TRIGGER","trigger_source":"REMOTE","state_before":"NORMAL","state_after":"ACK_WAIT","risk":2,"result":"WAITING_ACK","network_state":"ONLINE","power_state":"NORMAL","flags":0,"timestamp_ms":123456}'
  '{"node_id":"node01","event_id":1001,"scenario":"SOS_OR_FALL_SIM","event_type":"ACK_TIMEOUT","trigger_source":"LOCAL","state_before":"ACK_WAIT","state_after":"ALARM","risk":3,"result":"ESCALATED","network_state":"ONLINE","power_state":"NORMAL","flags":4,"timestamp_ms":153456}'
  '{"node_id":"node01","event_id":1001,"scenario":"SOS_OR_FALL_SIM","event_type":"CLEAR_ALARM","trigger_source":"REMOTE","state_before":"ALARM","state_after":"CLEARED","risk":0,"result":"CLEARED","network_state":"ONLINE","power_state":"NORMAL","flags":8,"timestamp_ms":160000}'
)

echo "Publishing fake event samples to ${TOPIC} via container ${CONTAINER}"

for payload in "${samples[@]}"; do
  echo "${payload}"
  docker exec "${CONTAINER}" mosquitto_pub -t "${TOPIC}" -m "${payload}"
  sleep "${DELAY_SECONDS}"
done

echo
echo "Done. Check analysis logs:"
echo "  docker logs -f eldercare-analysis"
echo
echo "Subscribe alarm topic:"
echo "  docker exec ${CONTAINER} mosquitto_sub -t eldercare/node01/alarm -C 1 -v"
