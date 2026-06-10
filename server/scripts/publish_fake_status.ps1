param(
    [string]$Container = "eldercare-mosquitto",
    [string]$Topic = "eldercare/node01/status",
    [int]$DelaySeconds = 2
)

$ErrorActionPreference = "Stop"

$samples = @(
    @{
        name = "normal"
        payload = '{"node_id":"node01","seq":9001,"temperature":25.6,"humidity":61.0,"gas":120,"presence":1,"risk":0,"event":"normal","relay_state_mask":0,"cloud_perm_mask":15}'
    },
    @{
        name = "notice"
        payload = '{"node_id":"node01","seq":9002,"temperature":29.0,"humidity":68.0,"gas":430,"presence":1,"risk":1,"event":"notice","relay_state_mask":1,"cloud_perm_mask":15}'
    },
    @{
        name = "warning"
        payload = '{"node_id":"node01","seq":9003,"temperature":31.5,"humidity":72.0,"gas":720,"presence":0,"risk":2,"event":"warning","relay_state_mask":5,"cloud_perm_mask":15}'
    },
    @{
        name = "alarm"
        payload = '{"node_id":"node01","seq":9004,"temperature":36.2,"humidity":88.0,"gas":980,"presence":1,"risk":3,"event":"alarm","relay_state_mask":15,"cloud_perm_mask":15}'
    }
)

Write-Host "Publishing fake status samples to $Topic via container $Container"

foreach ($sample in $samples) {
    Write-Host "[$($sample.name)] $($sample.payload)"
    docker exec $Container mosquitto_pub -t $Topic -m $($sample.payload)
    Start-Sleep -Seconds $DelaySeconds
}

Write-Host ""
Write-Host "Done. Check analysis logs:"
Write-Host "  docker logs -f eldercare-analysis"
Write-Host ""
Write-Host "Subscribe analysis topic:"
Write-Host "  docker exec $Container mosquitto_sub -t eldercare/node01/analysis -C 1 -v"
