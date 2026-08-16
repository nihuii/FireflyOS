$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

Write-Host '[1/5] Python contract tests'
python -m unittest discover -s (Join-Path $root 'tests\python') -v
if($LASTEXITCODE -ne 0) { throw 'Python tests failed' }

Write-Host '[2/5] Firmware core tests compile'
$coreTests = Join-Path $root 'tests\FireflyCoreTests\FireflyCoreTests.ino'
if(Test-Path -LiteralPath $coreTests) {
    & (Join-Path $PSScriptRoot 'build_firmware.ps1') -Target FireflyCoreTests
} else {
    Write-Host 'SKIP: core test sketch has not been introduced yet.'
}

Write-Host '[3/5] Audio hardware probe compile'
& (Join-Path $PSScriptRoot 'build_firmware.ps1') -Target AudioProbe

Write-Host '[4/5] Firefly firmware compile'
& (Join-Path $PSScriptRoot 'build_firmware.ps1') -Target Firefly

Write-Host '[5/5] Documentation checks'
python -m unittest tests.python.test_release_documentation -v
if($LASTEXITCODE -ne 0) { throw 'Release documentation tests failed' }

$docs = Get-ChildItem (Join-Path $root 'docs') -Recurse -File -Filter '*.md'
$markers = @(('T' + 'BD'), ('T' + 'ODO'), ('FIX' + 'ME'))
$bad = $docs | Select-String -Pattern ($markers -join '|')
if($bad) {
    $bad | ForEach-Object { Write-Error $_ }
    throw 'Placeholder found'
}

Write-Host 'All verification steps passed.'
