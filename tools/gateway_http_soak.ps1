param(
    [string]$BaseUrl = "http://169.254.1.1",
    [int]$DurationSeconds = 1800,
    [int]$IntervalSeconds = 10,
    [string]$OutputPath = "tools/logs/gateway_http_soak.jsonl"
)

$ErrorActionPreference = "Stop"
$absoluteOutput = [System.IO.Path]::GetFullPath((Join-Path (Get-Location) $OutputPath))
$outputDirectory = Split-Path -Parent $absoluteOutput
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null

$started = Get-Date
$deadline = $started.AddSeconds($DurationSeconds)
$samples = 0
$failures = 0
$firstUptime = $null
$lastUptime = $null
$uptimeRegressions = 0

function Write-SoakLine {
    param([string]$Path, [string]$Value)
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        try {
            Add-Content -LiteralPath $Path -Value $Value -Encoding utf8 -ErrorAction Stop
            return
        } catch [System.IO.IOException] {
            if ($attempt -eq 19) { throw }
            Start-Sleep -Milliseconds 100
        }
    }
}

while ((Get-Date) -lt $deadline) {
    $now = Get-Date
    try {
        $status = Invoke-RestMethod -Uri "$BaseUrl/api/debug/status" -TimeoutSec 4
        $warehouse = Invoke-RestMethod -Uri "$BaseUrl/api/warehouse/status" -TimeoutSec 4
        if ($null -eq $firstUptime) { $firstUptime = [int64]$status.uptime_ms }
        if ($null -ne $lastUptime -and [int64]$status.uptime_ms -lt $lastUptime) {
            $uptimeRegressions++
        }
        $lastUptime = [int64]$status.uptime_ms
        $record = [ordered]@{
            time = $now.ToString("o")
            ok = $true
            uptime_ms = [int64]$status.uptime_ms
            can_state = $status.can.state
            tx_ok = [int64]$status.can.tx_ok
            tx_fail = [int64]$status.can.tx_fail
            rx_frames = [int64]$status.laser.rx_frames
            rx_dropped = [int64]$status.can.rx_dropped
            bus_error = [int64]$status.can.bus_error
            form_errors = [int64]$status.can.form_errors
            laser_nodes = [int]$status.laser.node_count
            configured = [int]$warehouse.configured
            online = [int]$warehouse.online
            unknown = [int]$warehouse.unknown
        }
        $samples++
    } catch {
        $record = [ordered]@{
            time = $now.ToString("o")
            ok = $false
            error = $_.Exception.Message
        }
        $failures++
    }
    Write-SoakLine -Path $absoluteOutput -Value ($record | ConvertTo-Json -Compress)
    Start-Sleep -Seconds $IntervalSeconds
}

$summary = [ordered]@{
    event = "summary"
    started = $started.ToString("o")
    finished = (Get-Date).ToString("o")
    requested_duration_s = $DurationSeconds
    samples = $samples
    failures = $failures
    first_uptime_ms = $firstUptime
    last_uptime_ms = $lastUptime
    uptime_regressions = $uptimeRegressions
}
Write-SoakLine -Path $absoluteOutput -Value ($summary | ConvertTo-Json -Compress)
$summary | ConvertTo-Json -Compress
