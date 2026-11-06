param([ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference='Stop'
$dotnet=(Get-Command dotnet -ErrorAction Stop).Source
& $dotnet build (Join-Path $PSScriptRoot 'src\VoiceInput.App\VoiceInput.App.csproj') -c $Configuration -p:Platform=x64 -v minimal
if($LASTEXITCODE -ne 0){throw 'Build failed. See docs/BUILD.md for Windows prerequisites.'}
