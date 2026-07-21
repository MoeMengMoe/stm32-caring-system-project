# Heartbeat service

This lightweight MQTT service acknowledges valid `node01` status messages without
waiting for SQLite persistence, rules analysis, LLM calls, or notifications.

It preserves the existing wire protocol:

- subscribes to `eldercare/node01/status`
- publishes `eldercare/node01/ingest_ack`
- emits `{"node_id":"node01","seq":...,"stored":true}` with QoS 1

`stored: true` means that the heartbeat service accepted the status message. The
analysis service may publish the same idempotent acknowledgement after persistence.
