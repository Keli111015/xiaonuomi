#!/usr/bin/env pwsh

[CmdletBinding()]
param()

$ErrorActionPreference = "Stop"
$adfPath = $PSScriptRoot

# Always use the ESP-IDF checkout tracked by this repository, even if the
# caller has another IDF_PATH in its environment.
$idfPath = [System.IO.Path]::GetFullPath((Join-Path $adfPath "esp-idf"))
$env:ADF_PATH = $adfPath
$env:IDF_PATH = $idfPath

Write-Output "ADF_PATH: $env:ADF_PATH"
Write-Output "IDF_PATH: $env:IDF_PATH"

$idfExport = Join-Path $idfPath "export.ps1"
if (-not (Test-Path -LiteralPath $idfExport)) {
    Write-Error "Could not find ESP-IDF PowerShell export script: $idfExport"
    exit 1
}

# ESP-IDF's script emits assignments that must be evaluated in this session.
. $idfExport
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$patchScript = Join-Path $adfPath "tools\adf_install_patches.py"
& python.exe $patchScript apply-patch
if ($LASTEXITCODE -ne 0) {
    Write-Error "ESP-ADF IDF patch installation failed with exit code $LASTEXITCODE."
    exit $LASTEXITCODE
}

Write-Output ""
Write-Output "ESP-IDF environment setup completed successfully."
Write-Output "Run 'idf.py --help' or 'idf.py build' in this PowerShell session."
