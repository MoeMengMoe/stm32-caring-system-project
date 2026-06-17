param(
    [string]$Container = "eldercare-mosquitto",
    [string]$Topic = "eldercare/node01/event",
    [int]$DelaySeconds = 2
)

$ErrorActionPreference = "Stop"

$samples = @(
    @{
        name = "trigger"
        payload = '{"node_id":"node01","event_id":1001,"scenario":"SOS_OR_FALL_SIM","event_type":"REMOTE_TRIGGER","trigger_source":"REMOTE","state_before":"NORMAL","state_after":"ACK_WAIT","risk":2,"result":"WAITING_ACK","network_state":"ONLINE","power_state":"NORMAL","flags":0,"timestamp_ms":123456}'
    },
    @{
        name = "escalate"
        payload = '{"node_id":"node01","event_id":1001,"scenario":"SOS_OR_FALL_SIM","event_type":"ACK_TIMEOUT","trigger_source":"LOCAL","state_before":"ACK_WAIT","state_after":"ALARM","risk":3,"result":"ESCALATED","network_state":"ONLINE","power_state":"NORMAL","flags":4,"timestamp_ms":153456}'
    },
    @{
        name = "clear"
        payload = '{"node_id":"node01","event_id":1001,"scenario":"SOS_OR_FALL_SIM","event_type":"CLEAR_ALARM","trigger_source":"REMOTE","state_before":"ALARM","state_after":"CLEARED","risk":0,"result":"CLEARED","network_state":"ONLINE","power_state":"NORMAL","flags":8,"timestamp_ms":160000}'
    }
)

Write-Host "Publishing fake event samples to $Topic via container $Container"

foreach ($sample in $samples) {
    Write-Host "[$($sample.name)] $($sample.payload)"
    docker exec $Container mosquitto_pub -t $Topic -m $($sample.payload)
    Start-Sleep -Seconds $DelaySeconds
}

Write-Host ""
Write-Host "Done. Check analysis logs:"
Write-Host "  docker logs -f eldercare-analysis"
Write-Host ""
Write-Host "Subscribe alarm topic:"
Write-Host "  docker exec $Container mosquitto_sub -t eldercare/node01/alarm -C 1 -v"
