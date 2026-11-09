param(
    [string]$Port = 'COM12',
    [int]$Baud = 460800,
    [string]$Python = 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $projectRoot 'build'
if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'flash_args'))) {
    throw 'Build the project with idf.py build first.'
}
Push-Location -LiteralPath $buildDirectory
try {
    & $Python (Join-Path $PSScriptRoot 'esptool_local.py') --chip esp32p4 --port $Port --baud $Baud --before default_reset --after hard_reset write_flash '@flash_args'
    $flashExit = $LASTEXITCODE
} finally {
    Pop-Location
}
exit $flashExit
