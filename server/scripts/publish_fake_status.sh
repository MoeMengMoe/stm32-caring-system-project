#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${CONTAINER:-eldercare-mosquitto}"
TOPIC="${TOPIC:-eldercare/node01/status}"
DELAY_SECONDS="${DELAY_SECONDS:-2}"

samples=(
  '{"node_id":"node01","seq":9001,"temperature":25.6,"humidity":61.0,"gas":120,"presence":1,"risk":0,"event":"normal","relay_state_mask":0,"cloud_perm_mask":15}'
  '{"node_id":"node01","seq":9002,"temperature":29.0,"humidity":68.0,"gas":430,"presence":1,"risk":1,"event":"notice","relay_state_mask":1,"cloud_perm_mask":15}'
  '{"node_id":"node01","seq":9003,"temperature":31.5,"humidity":72.0,"gas":720,"presence":0,"risk":2,"event":"warning","relay_state_mask":5,"cloud_perm_mask":15}'
  '{"node_id":"node01","seq":9004,"temperature":36.2,"humidity":88.0,"gas":980,"presence":1,"risk":3,"event":"alarm","relay_state_mask":15,"cloud_perm_mask":15}'
)

echo "Publishing fake status samples to ${TOPIC} via container ${CONTAINER}"

for payload in "${samples[@]}"; do
  echo "${payload}"
  docker exec "${CONTAINER}" mosquitto_pub -t "${TOPIC}" -m "${payload}"
  sleep "${DELAY_SECONDS}"
done

echo
echo "Done. Check analysis logs:"
echo "  docker logs -f eldercare-analysis"
echo
echo "Subscribe analysis topic:"
echo "  docker exec ${CONTAINER} mosquitto_sub -t eldercare/node01/analysis -C 1 -v"
