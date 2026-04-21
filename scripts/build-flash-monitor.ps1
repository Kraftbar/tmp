param(
    [string]$Port = "COM6",
    [int]$FlashBaud = 115200,
    [int]$MonitorBaud = 115200
)

$ErrorActionPreference = "Stop"

$buildScript = Join-Path $PSScriptRoot "build.ps1"
$flashScript = Join-Path $PSScriptRoot "flash.ps1"
$monitorScript = Join-Path $PSScriptRoot "monitor.ps1"

& $buildScript
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $flashScript -Port $Port -Baud $FlashBaud
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

& $monitorScript -Port $Port -Baud $MonitorBaud

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
