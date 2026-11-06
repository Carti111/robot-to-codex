# Local portable build only. Does not install, stop other apps, or change startup.
param()
$ErrorActionPreference = 'Stop'
$projectRoot = $PSScriptRoot
& (Join-Path $projectRoot 'build.ps1') -Configuration Release
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
$buildRoot = Join-Path $projectRoot 'src\VoiceInput.App\bin\x64\Release\net8.0-windows10.0.19041.0\win-x64'
$distRoot = Join-Path $projectRoot 'dist'
$outputRoot = [IO.Path]::GetFullPath((Join-Path $distRoot 'CodexVoiceInputRobot'))
$allowedRoot = [IO.Path]::GetFullPath($distRoot).TrimEnd('\') + '\'
if (-not $outputRoot.StartsWith($allowedRoot,[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid output directory.' }
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
Get-ChildItem -LiteralPath $buildRoot -Force | Copy-Item -Destination $outputRoot -Recurse -Force
foreach ($name in @('LICENSE','THIRD_PARTY_NOTICES.md','ROBOT.md')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot $name) -Destination $outputRoot -Force
}
if (Test-Path -LiteralPath (Join-Path $projectRoot 'licenses')) {
    Copy-Item -LiteralPath (Join-Path $projectRoot 'licenses') -Destination $outputRoot -Recurse -Force
}
# Remove only known retired assemblies, with the output directory already validated above.
foreach ($obsolete in @(Get-ChildItem -LiteralPath $outputRoot -File | Where-Object { $_.Name -like 'NAudio*.dll' -or $_.Name -in @('VoiceInput.Protocol.dll','VoiceInput.Protocol.pdb') })) {
    $resolvedObsolete = [IO.Path]::GetFullPath($obsolete.FullName)
    if (-not $resolvedObsolete.StartsWith($outputRoot.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Invalid obsolete artifact path.' }
    Remove-Item -LiteralPath $resolvedObsolete -Force
}
Write-Output ('Robot build ready: ' + (Join-Path $outputRoot 'CodexVoiceInput.exe'))
