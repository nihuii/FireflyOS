param(
    [ValidateSet('Firefly', 'FireflyCoreTests', 'AudioProbe')]
    [string]$Target = 'Firefly'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$fqbn = 'esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,FlashSize=32M,PartitionScheme=app5M_fat24M_32MB,PSRAM=opi,LoopCore=0,EventsCore=0'
$sketch = switch($Target) {
    'Firefly' { Join-Path $root 'Firefly' }
    'FireflyCoreTests' { Join-Path $root 'tests\FireflyCoreTests' }
    'AudioProbe' { Join-Path $root 'examples\09_FireflyOS_AudioProbe' }
}
$buildPath = Join-Path $root ".build\$Target"
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

function Resolve-ArduinoCli {
    $candidates = @()
    if($env:ARDUINO_CLI) {
        $candidates += $env:ARDUINO_CLI
    }

    $pathCommand = Get-Command 'arduino-cli' -ErrorAction SilentlyContinue
    if($pathCommand) {
        $candidates += $pathCommand.Source
    }

    $candidates += @(
        'C:\APP\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe',
        (Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'),
        'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
    )

    foreach($candidate in $candidates) {
        if($candidate -and (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    throw 'arduino-cli was not found. Set ARDUINO_CLI or install Arduino IDE/Arduino CLI.'
}

$arduinoCli = Resolve-ArduinoCli
Write-Host "Arduino CLI: $arduinoCli"
Write-Host "Target: $Target"
Write-Host "FQBN: $fqbn"

& $arduinoCli compile `
    --fqbn $fqbn `
    --libraries (Join-Path $root 'libraries') `
    --build-path $buildPath `
    --warnings all `
    $sketch

if($LASTEXITCODE -ne 0) {
    throw "Arduino compile failed for $Target"
}
