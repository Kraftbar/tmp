param(
    [string]$Port = "COM6",
    [int]$Baud = 115200,
    [string]$ProjectName = "esp32_docker_starter",
    [int]$MaxAttempts = 30
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$buildDir = Join-Path $projectRoot "build"

$bootloader = Join-Path $buildDir "bootloader\bootloader.bin"
$partitionTable = Join-Path $buildDir "partition_table\partition-table.bin"
$app = Join-Path $buildDir "$ProjectName.bin"

foreach ($file in @($bootloader, $partitionTable, $app)) {
    if (-not (Test-Path $file)) {
        Write-Error "Missing build artifact: $file"
    }
}

function Invoke-FlashAttempt {
    param(
        [int]$FlashBaud,
        [string]$ResetSequence,
        [string]$BeforeMode = "default_reset"
    )

    Write-Host "Flashing $ProjectName on $Port at $FlashBaud baud (--before $BeforeMode)..."

    if ($ResetSequence) {
        Write-Host "Using slow reset sequence: $ResetSequence"
        $env:ESPTOOL_CUSTOM_RESET_SEQUENCE = $ResetSequence
    }
    else {
        Remove-Item Env:ESPTOOL_CUSTOM_RESET_SEQUENCE -ErrorAction SilentlyContinue
    }

    $stdoutFile = [System.IO.Path]::GetTempFileName()
    $stderrFile = [System.IO.Path]::GetTempFileName()
    $arguments = @(
        "-m", "esptool",
        "--chip", "esp32",
        "--port", $Port,
        "--baud", $FlashBaud,
        "--before", $BeforeMode,
        "--after", "hard_reset",
        "--connect-attempts", "5",
        "write_flash",
        "--flash_mode", "dio",
        "--flash_freq", "40m",
        "--flash_size", "keep",
        "0x1000", $bootloader,
        "0x8000", $partitionTable,
        "0x10000", $app
    )

    try {
        $process = Start-Process -FilePath "py" `
            -ArgumentList $arguments `
            -NoNewWindow `
            -Wait `
            -PassThru `
            -RedirectStandardOutput $stdoutFile `
            -RedirectStandardError $stderrFile

        $exitCode = $process.ExitCode
        $text = @()

        if (Test-Path $stdoutFile) {
            $text += Get-Content $stdoutFile -ErrorAction SilentlyContinue
        }

        if (Test-Path $stderrFile) {
            $text += Get-Content $stderrFile -ErrorAction SilentlyContinue
        }
    }
    finally {
        if (Test-Path $stdoutFile) {
            Remove-Item $stdoutFile -Force
        }

        if (Test-Path $stderrFile) {
            Remove-Item $stderrFile -Force
        }
    }

    $text | ForEach-Object { Write-Host $_ }

    return @{
        ExitCode = $exitCode
        Text = ($text -join "`n")
    }
}

$slowReset = "D0|R1|W0.1|D1|R0|W0.5|D0|W0.5"
$swappedReset = "R0|D1|W0.1|R1|D0|W0.5|R0|W0.5"
$retryBaud = if ($Baud -gt 115200) { 115200 } else { $Baud }

$strategies = @(
    @{ Baud = $Baud;      Reset = $slowReset;    Label = "slow reset" },
    @{ Baud = $retryBaud; Reset = $slowReset;    Label = "slow reset (retry)" },
    @{ Baud = $retryBaud; Reset = $swappedReset; Label = "swapped-polarity reset" }
)

$result = $null
for ($attempt = 1; $attempt -le $MaxAttempts; $attempt++) {
    $strategy = $strategies[($attempt - 1) % $strategies.Count]
    Write-Host ""
    Write-Host "=== Attempt $attempt of $MaxAttempts -- $($strategy.Label) ==="
    $result = Invoke-FlashAttempt -FlashBaud $strategy.Baud -ResetSequence $strategy.Reset

    if ($result.ExitCode -eq 0) {
        Write-Host "Flash succeeded on attempt $attempt."
        exit 0
    }

    if ($result.Text -match "Access is denied" -or $result.Text -match "PermissionError" -or $result.Text -match "could not open port") {
        Write-Warning "Serial port $Port is busy -- aborting loop."
        Write-Host "Close .\scripts\monitor.ps1 or any other serial program, then retry."
        exit $result.ExitCode
    }

    if ($attempt -lt $MaxAttempts) {
        Write-Warning "Attempt $attempt failed. Retrying in 2s... (Ctrl+C to stop)"
        Start-Sleep -Seconds 2
    }
}

Write-Warning "All $MaxAttempts attempts failed."
if ($result.Text -match "Wrong boot mode detected") {
    Write-Host "ESP32 never entered download mode across $MaxAttempts tries -- this is a USB power / cable / auto-reset hardware problem."
    Write-Host "Manual boot steps:"
    Write-Host "  1. Hold BOOT"
    Write-Host "  2. Run .\scripts\flash.ps1 -Port $Port -Baud 115200"
    Write-Host "  3. Tap EN or RST while still holding BOOT"
    Write-Host "  4. Release BOOT when 'Stub flasher running' appears"
}

exit $result.ExitCode
