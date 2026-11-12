param()
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$files=@(& git -C $root ls-files --cached --others --exclude-standard)
if($LASTEXITCODE -ne 0){throw 'Run this check inside a Git checkout.'}
$findings=[Collections.Generic.List[string]]::new()
$patterns=[ordered]@{
 'JWT'='eyJ[A-Za-z0-9_-]{15,}\.[A-Za-z0-9_-]{15,}\.[A-Za-z0-9_-]{15,}'
 'GitHub credential'='(?:gh[pousr]_[A-Za-z0-9]{25,}|github_pat_[A-Za-z0-9_]{40,})'
 'OpenAI-like secret'='sk-(?:proj-|svcacct-)?[A-Za-z0-9_-]{30,}'
 'endpoint credential'='wss://[^\s"''<>]+[?&]token=[A-Za-z0-9_.-]{40,}'
 'private key material'='-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----\r?\n(?:[A-Za-z0-9+/=]{40,}\r?\n){2,}'
 'author machine path'='C:[\\/]+Users[\\/]+Administrator(?:[\\/]|$)'
}
foreach($relative in $files){
 $path=Join-Path $root $relative
 if(-not (Test-Path -LiteralPath $path -PathType Leaf)){continue}
 $name=[IO.Path]::GetFileName($relative)
 $extension=[IO.Path]::GetExtension($name).ToLowerInvariant()
 # These are upstream board build definitions, not application runtime settings.
 $boardBuildConfig=$relative -match '^firmware/source/main/boards/[^/]+/config\.json$'
 if($boardBuildConfig){
  $definition=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
  if(@($definition.PSObject.Properties.Name | Where-Object {$_ -notin @('target','builds')}).Count -or
     -not $definition.target -or -not $definition.builds){
   $findings.Add($relative+': unexpected board build definition')
  }
  foreach($build in $definition.builds){
   if(@($build.PSObject.Properties.Name | Where-Object {$_ -notin @('name','sdkconfig_append')}).Count -or -not $build.name){
    $findings.Add($relative+': unexpected board build fields')
   }
  }
 }
 # The reference firmware embeds licensed UI prompts. Only accept Ogg containers
 # at the source asset path; recordings elsewhere remain forbidden.
 if($relative -match '^firmware/source/main/assets/(?:common|locales/[^/]+)/[^/]+\.ogg$'){
  $bytes=[IO.File]::ReadAllBytes($path)
  if($bytes.Length -lt 4 -or [Text.Encoding]::ASCII.GetString($bytes,0,4) -ne 'OggS'){
   $findings.Add($relative+': invalid firmware Ogg asset')
  }
  continue
 }
 if($relative -eq 'firmware/source/components/bmi270_sensorapi/bmi260-init-data.fw'){
  # Redistributable configuration image with adjacent license and pinned source.
  if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne 'cf0527e2a34aaf3acfdf5ca539cfb47c2381693d064b49d1479ebcfab0cca377'){
   $findings.Add($relative+': BMI260 configuration hash mismatch')
  }
  continue
 }
 if($relative -match '(^|/)(secrets|logs|dist|bin|obj|\.codex)/' -or
    $name -in @('auth.json','history.json','robot-wifi.bin','xiaozhi-endpoint.bin') -or
    ($name -eq 'config.json' -and -not $boardBuildConfig) -or
    ($name -like '.env*' -and $name -ne '.env.example') -or
    $extension -in @('.exe','.dll','.pdb','.bin','.pfx','.p12','.key','.pem','.crt','.log','.wav','.mp3','.ogg','.zip','.7z')){
    $findings.Add($relative+': forbidden runtime/private artifact');continue
 }
 if($extension -in @('.png','.ico','.jpg','.jpeg','.bmp')){continue}
 $text=[IO.File]::ReadAllText($path)
 foreach($pattern in $patterns.GetEnumerator()){
  if($text -cmatch $pattern.Value){$findings.Add($relative+': '+$pattern.Key)}
 }
}
if($findings.Count){$findings | ForEach-Object {Write-Output $_};throw 'Public source scan failed. Values suppressed.'}
Write-Output ('Public source check passed: '+$files.Count+' files; no configured secret/artifact matches.')
