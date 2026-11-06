$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$project=Join-Path $root 'examples\InMemoryRobot\InMemoryRobot.csproj'
& dotnet build $project -c Release -p:Platform=x64 -v minimal
if($LASTEXITCODE -ne 0){throw 'Example build failed.'}
& dotnet (Join-Path $root 'examples\InMemoryRobot\bin\x64\Release\net8.0-windows10.0.19041.0\InMemoryRobot.dll')
if($LASTEXITCODE -ne 0){throw 'Example failed.'}
