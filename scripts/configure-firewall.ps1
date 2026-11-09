#Requires -RunAsAdministrator
# Optional, manual, LocalSubnet-only rules for this specific built app. Never run by the app/build/CI.
param([string]$ProgramPath)
if (-not $ProgramPath) {
    $root = Split-Path -Parent $PSScriptRoot
    $ProgramPath = Join-Path $root 'CodexVoiceInput.exe'
    if (-not (Test-Path -LiteralPath $ProgramPath)) {
        $ProgramPath = Join-Path $root 'dist\CodexVoiceInputRobot\CodexVoiceInput.exe'
    }
}
$ErrorActionPreference='Stop'
$exe=(Resolve-Path -LiteralPath $ProgramPath).Path
$sha=[Security.Cryptography.SHA256]::Create()
try{$tag=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($exe)))).Replace('-','').Substring(0,10)}finally{$sha.Dispose()}
$prefix='CVI-Robot-'+$tag
foreach($definition in @(@{Suffix='TCP';Protocol='TCP';LocalPort=45831},@{Suffix='UDP';Protocol='UDP';LocalPort=45832},@{Suffix='Pair';Protocol='UDP';RemotePort=45833})){
 $name=$prefix+'-'+$definition.Suffix
 if(Get-NetFirewallRule -Name $name -ErrorAction SilentlyContinue){continue}
 $parameters=@{Name=$name;DisplayName=$name;Group='Robot to Codex';Direction='Inbound';Action='Allow';Program=$exe;Protocol=$definition.Protocol;RemoteAddress='LocalSubnet';Profile='Any'}
 if($definition.ContainsKey('LocalPort')){$parameters.LocalPort=$definition.LocalPort}
 if($definition.ContainsKey('RemotePort')){$parameters.RemotePort=$definition.RemotePort}
 New-NetFirewallRule @parameters | Out-Null
}
Write-Output ('Configured LAN-only rules for '+$exe)
