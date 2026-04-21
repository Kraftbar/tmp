param(
    [string]$Port = "COM6",
    [int]$Baud = 115200,
    [string]$LogFile = "serial.log"
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$tracer = Join-Path $PSScriptRoot "uart_tracer.py"
$logPath = Join-Path $projectRoot $LogFile

py $tracer --port $Port --baud $Baud --log-file $logPath

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
