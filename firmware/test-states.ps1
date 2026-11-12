[CmdletBinding()]
param([string]$ZigPath = 'zig')

$ErrorActionPreference = 'Stop'
$compiler = Get-Command $ZigPath -ErrorAction Stop
$outputDir = Join-Path $PSScriptRoot 'logs/native-state-tests'
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null
$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $outputDir 'global-cache'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $outputDir 'local-cache'
$names = @('feedback_state', 'menu_state', 'tilt_input', 'battery_state', 'camera_preview')
foreach ($name in $names) {
    $sourceFile = Join-Path $PSScriptRoot ("source/tests/{0}_test.cpp" -f $name)
    $binary = Join-Path $outputDir ("{0}_test.exe" -f $name)
    & $compiler.Source c++ -std=c++17 -O2 -fno-sanitize=all -nostdlib $sourceFile '-Wl,-e,mainCRTStartup' '-Wl,--subsystem,console' -o $binary
    if ($LASTEXITCODE -ne 0) { throw "Compile failed: $name" }
    & $binary
    if ($LASTEXITCODE -ne 0) { throw "State test failed: $name (exit $LASTEXITCODE)" }
    Write-Host "PASS $name"
}
Write-Host 'PASS all 5 firmware state test programs'
