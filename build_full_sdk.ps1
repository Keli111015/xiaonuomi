[CmdletBinding()]
param([switch]$ConfigureOnly)
$ErrorActionPreference = 'Stop'
$sdkRoot = Join-Path $PSScriptRoot 'sdk'
$localConfig = Join-Path $sdkRoot 'apps\main\cloud\ljyd_cloud_local.h'
if (-not (Test-Path -LiteralPath $localConfig)) {
    throw 'first replace sdk/apps/main/cloud/ljyd_cloud_local.example.h to ljyd_cloud_local.h , and fill in your PID and key'
}
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw 'please install sdk and perform export'
}
$env:ADF_PATH = $sdkRoot
$env:IDF_PATH = Join-Path $sdkRoot 'esp-idf'
$idfScript = Join-Path $env:IDF_PATH 'tools\idf.py'
$appRoot = Join-Path $sdkRoot 'apps'
if ($ConfigureOnly) { & python $idfScript -C $appRoot reconfigure }
else { & python $idfScript -C $appRoot build }
if ($LASTEXITCODE -ne 0) { throw "SDK构建失败，退出码 $LASTEXITCODE" }
