#!/usr/bin/env bash
set -euo pipefail

CONTAINER="${CONTAINER:-eldercare-mosquitto}"
TOPIC="${TOPIC:-eldercare/node01/status}"
PAYLOAD='{"node_id":"node01","seq":9101,"temperature":36.8,"humidity":86.0,"gas":990,"presence":1,"risk":3,"event":"alarm","relay_state_mask":3,"cloud_perm_mask":15}'

echo "Publishing LLM-trigger status to ${TOPIC} via container ${CONTAINER}"
echo "${PAYLOAD}"
docker exec "${CONTAINER}" mosquitto_pub -t "${TOPIC}" -m "${PAYLOAD}"

echo
echo "If OPENAI_API_KEY is configured in server/.env, analysis payload should contain model_used=true."
echo "Subscribe analysis topic:"
echo "  docker exec ${CONTAINER} mosquitto_sub -t eldercare/node01/analysis -C 1 -v"
echo
echo "Check service logs:"
echo "  docker logs -f eldercare-analysis"
