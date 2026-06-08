param(
    [string]$Container = "eldercare-mosquitto",
    [string]$Topic = "eldercare/node01/status"
)

$ErrorActionPreference = "Stop"

$payload = '{"node_id":"node01","seq":9101,"temperature":36.8,"humidity":86.0,"gas":990,"presence":1,"risk":3,"event":"alarm","relay_state_mask":3,"cloud_perm_mask":15}'

Write-Host "Publishing LLM-trigger status to $Topic via container $Container"
Write-Host $payload
docker exec $Container mosquitto_pub -t $Topic -m $payload

Write-Host ""
Write-Host "If OPENAI_API_KEY is configured, analysis payload should contain model_used=true."
Write-Host "Subscribe analysis topic:"
Write-Host "  docker exec $Container mosquitto_sub -t eldercare/node01/analysis -C 1 -v"
Write-Host ""
Write-Host "Check service logs:"
Write-Host "  docker logs -f eldercare-analysis"
