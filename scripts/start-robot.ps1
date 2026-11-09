$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$program = Join-Path $projectRoot 'dist\CodexVoiceInputRobot\CodexVoiceInput.exe'
if (-not (Test-Path -LiteralPath $program)) {
    Add-Type -AssemblyName PresentationFramework
    [System.Windows.MessageBox]::Show('Build the robot version using package.ps1 first.', 'Codex Voice Input')
    exit 1
}
# Explorer starts the WinUI app outside a packaged parent's identity and file virtualization.
# Normal launch already opens the settings window and activates an existing instance.
Start-Process -FilePath 'explorer.exe' -ArgumentList ('"' + $program + '"') -WindowStyle Hidden
