$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$program = Join-Path $projectRoot 'dist\CodexVoiceInputRobot\CodexVoiceInput.exe'
if (-not (Test-Path -LiteralPath $program)) {
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show('Build the robot version using package.ps1 first.', 'Codex Voice Input')
    exit 1
}
Start-Process -FilePath $program -ArgumentList "--open-settings" -WorkingDirectory (Split-Path -Parent $program) -WindowStyle Normal
