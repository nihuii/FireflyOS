param(
    [ValidateSet('Firefly', 'FireflyCoreTests', 'AudioProbe')]
    [string]$Target = 'Firefly',
    [ValidateSet('Development', 'Release')]
    [string]$Configuration = 'Development'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$releasePublicKey = Join-Path $root 'libraries\FireflyOS\src\firefly\services\FireflyUpdatePublicKey.local.h'
$releaseUpdateConfig = Join-Path $root 'libraries\FireflyOS\src\firefly\services\FireflyUpdateConfig.local.h'
if($Configuration -eq 'Release') {
    if($Target -ne 'Firefly') {
        throw 'Release configuration is only valid for the Firefly target'
    }
    if(-not (Test-Path -LiteralPath $releasePublicKey -PathType Leaf)) {
        throw 'Release OTA public key is missing: FireflyUpdatePublicKey.local.h'
    }
    if(-not (Test-Path -LiteralPath $releaseUpdateConfig -PathType Leaf)) {
        throw 'Release HTTPS configuration is missing: FireflyUpdateConfig.local.h'
    }
}
$fqbn = 'esp32:esp32:esp32s3:CPUFreq=240,FlashMode=qio,FlashSize=32M,PartitionScheme=app5M_fat24M_32MB,PSRAM=opi,LoopCore=0,EventsCore=0'
$sketch = switch($Target) {
    'Firefly' { Join-Path $root 'Firefly' }
    'FireflyCoreTests' { Join-Path $root 'tests\FireflyCoreTests' }
    'AudioProbe' { Join-Path $root 'examples\09_FireflyOS_AudioProbe' }
}
$buildPath = Join-Path $root ".build\$Target"
New-Item -ItemType Directory -Path $buildPath -Force | Out-Null

function Resolve-Python {
    if($env:PYTHON -and (Test-Path -LiteralPath $env:PYTHON -PathType Leaf)) {
        return (Resolve-Path -LiteralPath $env:PYTHON).Path
    }
    $command = Get-Command 'python' -ErrorAction SilentlyContinue
    if($command) {
        return $command.Source
    }
    throw 'Python was not found. Set PYTHON or install Python.'
}

$python = Resolve-Python
$partitionSource = if($Target -eq 'Firefly') {
    Join-Path $root 'Firefly\partitions.csv'
} else {
    Join-Path $PSScriptRoot 'partitions\app5M_fat24M_32MB.csv'
}
$partitionTarget = Join-Path $buildPath 'partitions.csv'
if(-not (Test-Path -LiteralPath $partitionSource -PathType Leaf)) {
    throw "Custom partition layout was not found: $partitionSource"
}
if($Target -eq 'Firefly') {
    & $python (Join-Path $root 'tools\validate_partition_layout.py') `
        --csv $partitionSource
    if($LASTEXITCODE -ne 0) {
        throw 'Partition source validation failed'
    }
}
Copy-Item -LiteralPath $partitionSource -Destination $partitionTarget -Force

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
$uploadMaximumSize = if($Target -eq 'Firefly') {
    'upload.maximum_size=11534336'
} else {
    'upload.maximum_size=4718592'
}
Write-Host "Arduino CLI: $arduinoCli"
Write-Host "Target: $Target"
Write-Host "Configuration: $Configuration"
Write-Host "FQBN: $fqbn"

$compileArguments = @(
    '--fqbn', $fqbn,
    '--build-property', $uploadMaximumSize,
    '--libraries', (Join-Path $root 'libraries'),
    '--build-path', $buildPath,
    '--warnings', 'all'
)
if($Configuration -eq 'Release') {
    $compileArguments += @(
        '--build-property',
        'compiler.cpp.extra_flags=-DFIREFLY_RELEASE_BUILD=1'
    )
}
$compileArguments += $sketch
& $arduinoCli compile @compileArguments

if($LASTEXITCODE -ne 0) {
    throw "Arduino compile failed for $Target"
}

if($Target -eq 'Firefly') {
    $compiledPartition = Join-Path $buildPath 'Firefly.ino.partitions.bin'
    if(-not (Test-Path -LiteralPath $compiledPartition -PathType Leaf)) {
        throw "Compiled partition table was not found: $compiledPartition"
    }
    & $python (Join-Path $root 'tools\validate_partition_layout.py') `
        --csv $partitionSource `
        --binary $compiledPartition
    if($LASTEXITCODE -ne 0) {
        throw 'Compiled partition validation failed'
    }

    $firmwareLimit = 9227468
    $firmwarePath = Join-Path $buildPath 'Firefly.ino.bin'
    $firmware = Get-Item -LiteralPath $firmwarePath -ErrorAction Stop
    if($firmware.Length -gt $firmwareLimit) {
        throw "Firmware exceeds 80% OTA slot budget: $($firmware.Length) > $firmwareLimit"
    }
    Write-Host "OTA slot budget: $($firmware.Length) / $firmwareLimit bytes"
}
