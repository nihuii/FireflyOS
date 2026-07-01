param(
    [string]$FontSource = $env:FIREFLY_FONT_SOURCE,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$glyphFile = Join-Path $PSScriptRoot 'system_glyphs.txt'
$glyphSeed = Join-Path $PSScriptRoot 'system_glyphs_seed.txt'
$glyphCollector = Join-Path $PSScriptRoot 'collect_system_glyphs.py'
if(-not $OutputDirectory) {
    $OutputDirectory = Join-Path $root 'Firefly\assets\fonts'
}

python $glyphCollector `
    --root (Join-Path $root 'Firefly') (Join-Path $root 'libraries\FireflyOS\src') `
    --seed $glyphSeed `
    --output $glyphFile
if($LASTEXITCODE -ne 0) {
    throw 'Failed to collect FireflyOS system glyphs.'
}

if(-not $FontSource -or -not (Test-Path -LiteralPath $FontSource -PathType Leaf)) {
    throw 'Set FIREFLY_FONT_SOURCE or pass -FontSource with a CJK TTF/OTF file.'
}

$converter = Get-Command 'lv_font_conv' -ErrorAction SilentlyContinue
if(-not $converter) {
    throw 'lv_font_conv is not installed or is not available on PATH.'
}

$glyphs = (Get-Content -LiteralPath $glyphFile -Raw -Encoding UTF8) -replace '\s', ''
if(-not $glyphs) {
    throw 'system_glyphs.txt is empty.'
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
foreach($size in @(18, 22, 24)) {
    $output = Join-Path $OutputDirectory "firefly_cn_$size.c"
    & $converter.Source `
        --font $FontSource `
        --size $size `
        --bpp 4 `
        --format lvgl `
        --symbols $glyphs `
        --output $output `
        --force-fast-kern-format
    if($LASTEXITCODE -ne 0) {
        throw "lv_font_conv failed for ${size}px."
    }
}

Write-Host "Generated FireflyOS CJK font subsets in $OutputDirectory"
