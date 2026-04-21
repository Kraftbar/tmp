param(
    [string]$Image = "espressif/idf:v5.2.2"
)

$ErrorActionPreference = "Stop"

$projectRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

docker run --rm `
    -v "${projectRoot}:/project" `
    -w /project `
    $Image `
    idf.py set-target esp32 build

if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}
