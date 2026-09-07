#!/usr/bin/env pwsh
#Requires -Version 5.1
# Read the PE fingerprint (TimeDateStamp / SizeOfImage / CheckSum) from an
# SnowRunner.exe on disk and compare it against every build profile in
# src/builds/. First thing to run when a user reports the dormant
# "unknown build" log line, and the first step of a post-patch rederive.
#
# This answers "does this EXE have a profile", and nothing more. It does not
# check whether the pinned camera slot and transform offset still hold on a new
# build - that rederive is a separate job, and the template printed below
# carries the CURRENT profile's numbers, not measurements of this EXE.

[CmdletBinding()]
param([string]$ExePath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

if (-not $ExePath) {
    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $gamePath = Find-GamePath -GameId 'snowrunner'
    if (-not $gamePath) { throw "SnowRunner not found. Pass -ExePath explicitly." }
    # SnowRunner.exe is nested; the install root is what Find-GamePath returns.
    $ExePath = Join-Path $gamePath 'Sources\Bin\SnowRunner.exe'
}
if (-not (Test-Path $ExePath)) { throw "EXE not found: $ExePath" }

$bytes = [System.IO.File]::ReadAllBytes($ExePath)
# Every offset below is taken from the file, so each one is range-checked before
# it is used. Without this a truncated download or a text file passed by mistake
# comes back as a raw ArgumentException naming an array index, which tells the
# person running this nothing about what they actually handed it.
if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
    throw "Not a PE image (no MZ header): $ExePath"
}
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ($peOffset -lt 0 -or $peOffset -gt ($bytes.Length - 4)) {
    throw "Not a PE image (PE header offset 0x{0:X} is outside the file): {1}" -f $peOffset, $ExePath
}
if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) { throw "Not a PE image: $ExePath" }

$coff    = $peOffset + 4
$optHdr  = $coff + 20
# SizeOfImage sits at optional-header +56 and CheckSum at +64, so the header has
# to reach at least +68.
if (($optHdr + 68) -gt $bytes.Length) {
    throw "PE optional header is truncated: $ExePath"
}
$timeDateStamp = [BitConverter]::ToUInt32($bytes, $coff + 4)
$sizeOfImage   = [BitConverter]::ToUInt32($bytes, $optHdr + 56)
$checkSum      = [BitConverter]::ToUInt32($bytes, $optHdr + 64)

$built = [DateTimeOffset]::FromUnixTimeSeconds($timeDateStamp).UtcDateTime
Write-Host "EXE: $ExePath"
Write-Host ("  TimeDateStamp 0x{0:X8}  ({1:yyyy-MM-dd HH:mm:ss} UTC)" -f $timeDateStamp, $built)
Write-Host ("  SizeOfImage   0x{0:X8}" -f $sizeOfImage)
Write-Host ("  CheckSum      0x{0:X8}" -f $checkSum)
Write-Host ""

$offsetsFile = Join-Path $projectDir 'src/builds/steam_offsets.cpp'
$known = Select-String -Path $offsetsFile -Pattern '\{\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8})\s*\}'
$matched = $false
foreach ($k in $known) {
    $t = [Convert]::ToUInt32($k.Matches[0].Groups[1].Value, 16)
    $s = [Convert]::ToUInt32($k.Matches[0].Groups[2].Value, 16)
    $c = [Convert]::ToUInt32($k.Matches[0].Groups[3].Value, 16)
    if ($t -eq $timeDateStamp -and $s -eq $sizeOfImage -and $c -eq $checkSum) {
        Write-Host "MATCH: this build already has a profile (steam_offsets.cpp line $($k.LineNumber))." -ForegroundColor Green
        $matched = $true
    }
}

if (-not $matched) {
    # The offsets block is lifted verbatim out of the newest profile in
    # steam_offsets.cpp rather than written out here. A hand-kept copy is what
    # this script shipped before, and it had drifted a whole design behind the
    # struct: it printed camera_compute_slot / camera_out_transform /
    # camera_out_transform_floats, none of which are fields of OffsetTable any
    # more, so following it produced a profile that would not compile.
    $offsetsText = Get-Content -Raw $offsetsFile
    # Name, then the fingerprint braces, then the offsets braces - the second
    # inner group is the one wanted, so the fingerprint's is matched explicitly
    # rather than skipped over.
    $blocks = [regex]::Matches($offsetsText,
        '(?s)extern\s+const\s+BuildProfile\s+\w+\s*=\s*\{[^{}]*\{[^{}]*\}\s*,\s*\{(.*?)\}\s*,\s*\}\s*;')
    if ($blocks.Count -eq 0) {
        throw "Could not read an existing profile's offsets block from $offsetsFile."
    }
    # The LAST block, not the first: steam_offsets.cpp is append-only, so the
    # newest profile is at the bottom of the file. Only kKnownProfiles in
    # build_registry.cpp is ordered newest-first. Seeding a rederive from the
    # oldest profile's RVAs starts it at a number that looks authoritative and
    # is several patches stale.
    $newest = $blocks[$blocks.Count - 1]
    $newestName = if ($newest.Value -match 'kSteamProfile_\d+') { $Matches[0] } else { 'the last profile in the file' }
    $offsetLines = $newest.Groups[1].Value -split "`n" |
        Where-Object { $_.Trim() } |
        ForEach-Object { '        ' + $_.Trim() }

    Write-Host "No profile matches this EXE. Append a new profile to src/builds/steam_offsets.cpp:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host ("extern const BuildProfile kSteamProfile_{0:yyyyMMdd} = {{" -f $built)
    Write-Host ("    `"steam-win64-{0:yyyyMMdd}`"," -f $built)
    Write-Host ("    {{ 0x{0:X8}, 0x{1:X8}, 0x{2:X8} }}," -f $timeDateStamp, $sizeOfImage, $checkSum)
    Write-Host "    {"
    $offsetLines | ForEach-Object { Write-Host $_ }
    Write-Host "    },"
    Write-Host "};"
    Write-Host ""
    Write-Host "Then add it to the TOP of kKnownProfiles in src/builds/build_registry.cpp."
    Write-Host ("Every number in that block is COPIED from {0}, not measured from this" -f $newestName)
    Write-Host "EXE. Rederive and confirm them before shipping. Until then set"
    Write-Host "camera_update_rva, camera_transform and camera_transform_floats to 0:"
    Write-Host "IsProfileComplete() checks all three, so a zero in any of them routes the"
    Write-Host "build to a profile that stays dormant."
}
