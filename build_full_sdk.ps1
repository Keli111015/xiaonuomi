[CmdletBinding()]
param([switch]$ConfigureOnly)
$ErrorActionPreference = 'Stop'
$sdkRoot = Join-Path $PSScriptRoot 'sdk'
$localConfig = Join-Path $sdkRoot 'apps\main\cloud\ljyd_cloud_local.h'
if (-not (Test-Path -LiteralPath $localConfig)) {
    throw '先把 sdk/apps/main/cloud/ljyd_cloud_local.example.h 复制为 ljyd_cloud_local.h，填写自己的PID和产品密钥。'
}
if (-not (Get-Command python -ErrorAction SilentlyContinue)) {
    throw '请先安装SDK工具链并在当前PowerShell导出ESP-IDF环境。'
}
$env:ADF_PATH = $sdkRoot
$env:IDF_PATH = Join-Path $sdkRoot 'esp-idf'
$idfScript = Join-Path $env:IDF_PATH 'tools\idf.py'
$appRoot = Join-Path $sdkRoot 'apps'
if ($ConfigureOnly) { & python $idfScript -C $appRoot reconfigure }
else { & python $idfScript -C $appRoot build }
if ($LASTEXITCODE -ne 0) { throw "SDK构建失败，退出码 $LASTEXITCODE" }
