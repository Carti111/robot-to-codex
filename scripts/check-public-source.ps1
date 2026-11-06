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
 if($relative -match '(^|/)(secrets|logs|dist|bin|obj|\.codex)/' -or
    $name -in @('auth.json','config.json','history.json','robot-wifi.bin','xiaozhi-endpoint.bin') -or
    ($name -like '.env*' -and $name -ne '.env.example') -or
    $extension -in @('.exe','.dll','.pdb','.bin','.pfx','.p12','.key','.pem','.crt','.log','.wav','.mp3','.ogg','.zip','.7z')){
    $findings.Add($relative+': forbidden runtime/private artifact');continue
 }
 if($extension -in @('.png','.ico')){continue}
 $text=[IO.File]::ReadAllText($path)
 foreach($pattern in $patterns.GetEnumerator()){
  if($text -cmatch $pattern.Value){$findings.Add($relative+': '+$pattern.Key)}
 }
}
if($findings.Count){$findings | ForEach-Object {Write-Output $_};throw 'Public source scan failed. Values suppressed.'}
Write-Output ('Public source check passed: '+$files.Count+' files; no configured secret/artifact matches.')
