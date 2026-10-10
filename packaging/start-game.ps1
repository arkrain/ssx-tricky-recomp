param(
    [string]$GameDir = (Join-Path $PSScriptRoot 'game'),
    [string]$SaveDir = (Join-Path $PSScriptRoot 'saves'),
    [switch]$SkipIntro,
    [switch]$Software,
    [switch]$NoAudio
)
$ErrorActionPreference = 'Stop'
try {
    $binary = Join-Path $PSScriptRoot 'ssx_tricky.exe'
    if (!(Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw 'The Windows executable is missing from this folder.'
    }
    $game = (Resolve-Path -LiteralPath $GameDir).Path
    $xbe = Join-Path $game 'default.xbe'
    $expected = '0128bbf6f86cc27307156f9206245115a9950541317d05b7037cab6ef063c84a'
    if ((Get-FileHash -LiteralPath $xbe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
        throw 'This port requires the original Xbox US retail game image.'
    }
    $save = [IO.Path]::GetFullPath($SaveDir)
    [IO.Directory]::CreateDirectory($save) | Out-Null
    $env:RECOMP_PB_EXEC = '1'
    $env:RECOMP_VBLANK = '1'
    $env:RECOMP_FB_WINDOW = '1'
    $env:RECOMP_USB = '1'
    $env:RECOMP_RENDER_EVERY = '1'
    $env:RECOMP_NV2A_IDLE_BACKOFF = '1'
    $env:SSX_GL = $(if ($Software) { '0' } else { '1' })
    $env:RECOMP_AC97_READY = $(if ($NoAudio) { $null } else { '1' })
    $env:SSX_SKIP_INTRO = $(if ($SkipIntro) { '1' } else { $null })
    Push-Location -LiteralPath $PSScriptRoot
    try {
        & $binary $game $save
        $gameExit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    exit $gameExit
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
