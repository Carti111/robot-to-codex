param([string]$CompilerPath, [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
if (-not $SkipBuild) {
    & (Join-Path $projectRoot 'package.ps1')
    if ($LASTEXITCODE -ne 0) { throw 'Portable application build failed.' }
}
[xml]$project = Get-Content -LiteralPath (Join-Path $projectRoot 'src\VoiceInput.App\VoiceInput.App.csproj') -Raw
$version = [string]$project.Project.PropertyGroup.Version
$payload = [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist\CodexVoiceInputRobot'))
if (-not (Test-Path -LiteralPath (Join-Path $payload 'CodexVoiceInput.exe'))) { throw 'Portable application not found.' }
if (-not $CompilerPath) {
    $candidates = @()
    if (${env:ProgramFiles(x86)}) { $candidates += Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
    $CompilerPath = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $CompilerPath) {
        $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($command) { $CompilerPath = $command.Source }
    }
}
if (-not $CompilerPath -or -not (Test-Path -LiteralPath $CompilerPath)) { throw 'Build needs the Inno Setup 6 compiler (ISCC.exe).' }
$release = Join-Path $projectRoot 'dist\releases'
New-Item -ItemType Directory -Path $release -Force | Out-Null
foreach ($symbol in @(Get-ChildItem -LiteralPath $payload -File -Filter '*.pdb' -Recurse)) {
    if (-not $symbol.FullName.StartsWith($payload.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected symbol path.' }
    Remove-Item -LiteralPath $symbol.FullName -Force
}
foreach ($file in @(Get-ChildItem -LiteralPath $payload -File -Recurse)) {
    if ($file.Name -match '^(\.env($|\.)|auth\.json$|config\.json$|history\.json$|robot-wifi\.bin$|robot-pairing\.key$|manager-config\.bin$|xiaozhi-endpoint\.bin$)' -or
        $file.Extension -in @('.key','.pem','.pfx','.p12','.crt','.log') -or $file.FullName -match '[\\/]secrets[\\/]') { throw 'Private runtime data cannot be distributed.' }
}
& $CompilerPath /Q ("/DMyAppVersion=" + $version) (Join-Path $projectRoot 'installer\CodexVoiceInput.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed.' }
$setup = Join-Path $release ("RobotToCodex-Setup-" + $version + ".exe")
$zip = Join-Path $release ("RobotToCodex-win-x64-" + $version + ".zip")
Compress-Archive -Path (Join-Path $payload '*') -DestinationPath $zip -CompressionLevel Optimal -Force
$instructions = @(
    'Robot to Codex - Windows x64',
    'Install the Setup EXE, or fully extract the portable ZIP and double-click Start.cmd.',
    'Use compatible robot firmware; enable pairing on the robot and enter its six-digit code.',
    'Select a Codex chat and click the composer. Say 开启语音输入, dictate, then say 发送.',
    'Say 退出模式 to return to normal robot chat. The computer app must stay running.',
    'No MCP endpoint or Codex API key is required. ASR is supplied by your robot firmware.',
    'See https://github.com/Carti111/robot-to-codex for setup and robot integration.',
    'User data is stored separately under LocalAppData/CodexVoiceInputRobot.'
) -join [Environment]::NewLine
[IO.File]::WriteAllText((Join-Path $release '使用说明.txt'), $instructions, [Text.UTF8Encoding]::new($false))
$lines = foreach ($asset in @($setup,$zip)) {
    ((Get-FileHash -LiteralPath $asset -Algorithm SHA256).Hash.ToLowerInvariant()) + '  ' + [IO.Path]::GetFileName($asset)
}
[IO.File]::WriteAllLines((Join-Path $release 'SHA256SUMS.txt'), $lines, [Text.Encoding]::ASCII)
Write-Output ('Installer: ' + $setup)
Write-Output ('Portable: ' + $zip)
