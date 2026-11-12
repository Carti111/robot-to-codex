# 从源码编译机器人固件（通用路径版）
# 用法：在 ESP-IDF 5.5 PowerShell 中运行，或直接运行本脚本（脚本会尝试加载官方 profile）。

[CmdletBinding()]
param(
    [string]$LogDir
)

$ErrorActionPreference = 'Stop'
$projectDir = $PSScriptRoot
Set-Location $projectDir
if (-not $LogDir) { $LogDir = Join-Path $PSScriptRoot 'logs' }

# 1. 如果当前终端还找不到 idf.py，尝试加载 ESP-IDF 5.5.6 的 PowerShell profile。
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    $profiles = @(
        'C:\Espressif\tools\Microsoft.v5.5.6.PowerShell_profile.ps1',
        (Join-Path $env:USERPROFILE 'esp\esp-idf\export.ps1')
    )
    if ($env:IDF_PATH) {
        $profiles += (Join-Path $env:IDF_PATH 'export.ps1')
    }
    foreach ($profile in $profiles) {
        if (Test-Path $profile) {
            Write-Host "加载 ESP-IDF 环境: $profile"
            . $profile
            break
        }
    }
}

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    throw @'
找不到 idf.py。请先安装 ESP-IDF 5.5.6，然后从开始菜单打开 "ESP-IDF 5.5 PowerShell" 再运行本脚本。
安装地址：https://dl.espressif.com/dl/esp-idf/
'@
}

$version = (& idf.py --version) 2>&1 | Out-String
Write-Host $version.Trim()
if ($version -notmatch 'v5\.5\.6') {
    Write-Warning '当前 ESP-IDF 不是 v5.5.6，编译结果可能与本产品不一致。'
}

# 2. 固定目标、编码和并行度。
$env:IDF_TARGET = 'esp32s3'
$env:PYTHONUTF8 = '1'
$env:CMAKE_BUILD_PARALLEL_LEVEL = '2'

# 3. 写日志。
New-Item -ItemType Directory -Force $LogDir | Out-Null
$logFile = Join-Path $LogDir ("build-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

# 4. 使用与产品一致的 sdkconfig 组合构建。
$sdkDefaults = 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.robot.defaults'

Write-Host "开始编译: $projectDir"
Write-Host "日志: $logFile"
& idf.py -D $sdkDefaults build 2>&1 | Tee-Object -FilePath $logFile
$exitCode = $LASTEXITCODE

if ($exitCode -ne 0) {
    throw "编译失败，退出码 $exitCode。请查看日志：$logFile"
}

Write-Host ''
Write-Host '编译成功。烧录文件：'
Write-Host "  应用：$projectDir\build\xiaozhi.bin"
Write-Host "  完整：见 build\bootloader、build\partition_table、build\ota_data_initial.bin、build\srmodels"
