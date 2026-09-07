#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev loop: copy the built .asi and the vendored ASI loader into the game folder.

[CmdletBinding()]
param(
    # Positional so `deploy.ps1 "D:\Games\SnowRunner"` works, matching the
    # positional game path install.cmd takes. Named -Config stays available.
    [Parameter(Position = 0)][string]$GamePath,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force

if (-not $GamePath) {
    $GamePath = Find-GamePath -GameId 'snowrunner'
}
if (-not $GamePath -or -not (Test-Path $GamePath)) {
    throw "SnowRunner not found. Pass -GamePath explicitly."
}

# SnowRunner.exe lives under Sources\Bin, not the install root, and everything
# the mod needs - the loader, the .asi, HeadTracking.ini and the log - has to
# sit beside the exe.
$binDir = Join-Path $GamePath 'Sources\Bin'
if (-not (Test-Path (Join-Path $binDir 'SnowRunner.exe'))) {
    throw "SnowRunner.exe is not at $binDir - pass the install root, not the Bin folder."
}

$asi = Join-Path $projectDir "build/$Config/SnowRunnerHeadTracking.asi"
if (-not (Test-Path $asi)) { throw "Build output not found: $asi. Run 'pixi run build' first." }

$loader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
if (-not (Test-Path $loader)) { throw "Vendored ASI loader missing. Run 'pixi run update-deps'." }

Copy-Item $asi (Join-Path $binDir 'SnowRunnerHeadTracking.asi') -Force
Write-Host "  deployed SnowRunnerHeadTracking.asi" -ForegroundColor DarkGray

# SnowRunner.exe imports DINPUT8.dll directly, so the loader takes that name and
# the game-local copy wins over the system one.
$loaderTarget = Join-Path $binDir 'dinput8.dll'
if (-not (Test-Path $loaderTarget)) {
    Copy-Item $loader $loaderTarget -Force
    Write-Host "  deployed dinput8.dll (Ultimate ASI Loader)" -ForegroundColor DarkGray
} else {
    Write-Host "  dinput8.dll already present, left alone" -ForegroundColor DarkGray
}

Write-Host "Deployed to $binDir" -ForegroundColor Green
