#!/usr/bin/env pwsh
#Requires -Version 5.1
# Bump the vendored Ultimate ASI Loader copy to the latest upstream release
# inside the pinned range. Manual: run it, review the diff under vendor/, then
# commit. Nothing in the build, package or release path calls this - CI
# consumes whatever is committed under vendor/.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'git submodule update --init --recursive'."
}
Import-Module $module -Force

$vendorDir  = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorDll  = Join-Path $vendorDir 'dinput8.dll'
$readmePath = Join-Path $vendorDir 'README.md'

# Snapshot the current state so an unchanged upstream can be put back byte for
# byte. Update-VendoredLoader's own idempotency check compares the on-disk file
# against the release zip, and what we keep here is the DLL unwrapped from that
# zip, so the hashes never match and every run would otherwise rewrite README.md
# with a fresh "Fetched at" - a no-op refresh that reads as a loader bump.
# Hashed through .NET rather than Get-FileHash, for the reason
# package-release.ps1 records at its own hash: Get-FileHash came back
# CommandNotFound in the shell pixi hands a task, while every binary cmdlet
# resolved normally. This script writes the SHA that the packager's release
# gate later verifies, so it must not be the one link that depends on module
# autoloading.
function Get-Sha256Hex([string]$Path) {
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $stream = [System.IO.File]::OpenRead($Path)
        try { return [System.BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
        finally { $stream.Dispose() }
    } finally { $sha.Dispose() }
}

$previousDllSha = if (Test-Path $vendorDll) { Get-Sha256Hex $vendorDll } else { $null }
$previousReadme = if (Test-Path $readmePath) { [System.IO.File]::ReadAllBytes($readmePath) } else { $null }

Update-VendoredLoader `
    -Name 'ultimate-asi-loader' `
    -OutputDir $vendorDir `
    -OutputFileName 'dinput8.dll' `
    -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
    -VersionPrefix 'v9.' `
    -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$' `
    -LicenseUrl 'https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/master/license' | Out-Null

# The x64 asset is a wrapper zip around a single dinput8.dll, so what the module
# just saved under that name is the zip. Replace it with the DLL inside.
$bytes = [System.IO.File]::ReadAllBytes($vendorDll)
if ($bytes.Length -ge 2 -and $bytes[0] -eq 0x50 -and $bytes[1] -eq 0x4B) {
    # Under TEMP, never inside $vendorDir: package-release.ps1 copies
    # vendor/* recursively into the release ZIP, so a staging directory left
    # behind here would ship a second copy of the loader and the wrapper zip
    # inside the installer.
    $stage = Join-Path ([System.IO.Path]::GetTempPath()) ("sr-ht-loader-" + [System.Guid]::NewGuid().ToString('N'))
    if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    New-Item -ItemType Directory -Path $stage | Out-Null
    try {
        $zipCopy = Join-Path $stage 'loader.zip'
        Copy-Item $vendorDll $zipCopy
        Expand-Archive -Path $zipCopy -DestinationPath $stage -Force
        $dll = Get-ChildItem -Path $stage -Recurse -Filter 'dinput8.dll' | Select-Object -First 1
        if (-not $dll) { throw "x64 dinput8.dll not found inside Ultimate-ASI-Loader_x64.zip." }
        Copy-Item $dll.FullName $vendorDll -Force
    } finally {
        if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
    }
}

$dllSha = Get-Sha256Hex $vendorDll

if ($previousReadme -and $dllSha -eq $previousDllSha) {
    [System.IO.File]::WriteAllBytes($readmePath, $previousReadme)
    Write-Host ""
    Write-Host "vendor/ultimate-asi-loader is already current (dinput8.dll sha256=$($dllSha.Substring(0, 12))...)." -ForegroundColor Green
    return
}

# The module's SHA-256 line covers the wrapper zip, which is not what gets
# committed, so record the unwrapped DLL's hash next to it.
$lines  = Get-Content $readmePath
$anchor = ($lines | Select-String -SimpleMatch '- SHA-256:' | Select-Object -First 1).LineNumber
if (-not $anchor) { throw "vendor README.md has no '- SHA-256:' line to anchor the DLL hash after." }
$lines = @($lines[0..($anchor - 1)]) + "- dinput8.dll SHA-256: ``$dllSha``" + @($lines[$anchor..($lines.Count - 1)])

$lines = $lines | ForEach-Object {
    if ($_ -like 'Do not edit this directory by hand.*') {
        'Do not edit this directory by hand. Run `pixi run update-deps` to refresh, then commit.'
    } else {
        $_
    }
}

# WriteAllLines with an explicit BOM-less encoder: `-Encoding utf8` in Windows
# PowerShell 5.1 writes a BOM, which is why this file was the only one in the
# repo starting with EF BB BF - and it ships in the release ZIP.
[System.IO.File]::WriteAllLines($readmePath, $lines, (New-Object System.Text.UTF8Encoding $false))

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed (dinput8.dll sha256=$($dllSha.Substring(0, 12))...). Review and commit." -ForegroundColor Green
