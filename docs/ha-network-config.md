# HA Network Config

## Changes
- network_mode: host (was bridge)
- depends_on: mosquitto condition: service_healthy
- Ports mapping removed
- MQTT broker: mosquitto -> 127.0.0.1
- Docker proxy configured: 192.168.10.186:7897
- HACS installed to custom_components/hacs/
