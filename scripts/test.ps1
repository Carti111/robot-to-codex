param([switch]$IncludeClipboard)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$projects=@('RobotContract','TextPostProcessorContract')
if($IncludeClipboard){$projects+='ClipboardContract'}
foreach($name in $projects){
 $project=Join-Path $root ("tests\$name\$name.csproj")
 & dotnet build $project -c Release -p:Platform=x64 -v minimal
 if($LASTEXITCODE -ne 0){throw "Test build failed: $name"}
 [xml]$definition=Get-Content -LiteralPath $project -Raw
 $framework=[string]$definition.Project.PropertyGroup.TargetFramework
 $test=Join-Path $root ("tests\$name\bin\x64\Release\$framework\$name.dll")
 & dotnet $test
 if($LASTEXITCODE -ne 0){throw "Contract failed: $name"}
}
