#!/usr/bin/env pwsh
#Requires -Version 5.1
# Read the PE fingerprint (TimeDateStamp / SizeOfImage / CheckSum) from an
# SnowRunner.exe on disk and compare it against every build profile in
# src/builds/. First thing to run when a user reports the dormant
# "unknown build" log line, and the first step of a post-patch rederive.
#
# With no -ExePath it checks EVERY copy of the game on this machine, not the
# first one detection returns. A store variant is a separate binary with its
# own RVAs and its own profile, so a machine with a Steam copy and a Game Pass
# copy has two builds to answer for and only one of them is the one you last
# looked at.
#
# This answers "does this EXE have a profile", and nothing more. It does not
# check whether the pinned camera addresses still hold on a new build - that
# rederive is a separate job, and the template printed below carries the
# CURRENT profile's numbers, not measurements of this EXE.

[CmdletBinding()]
param(
    [string]$ExePath,
    # Which store's profile file and naming the printed template belongs to.
    # Only consulted with -ExePath; an enumerated copy is labelled from where
    # it was found.
    [ValidateSet('steam', 'gdk')][string]$Store = 'steam'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

function Get-CheckTargets {
    if ($ExePath) {
        return @([pscustomobject]@{ Path = $ExePath; Store = $Store })
    }

    Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/GamePathDetection.psm1') -Force
    $config = Get-GameConfig -GameId 'snowrunner'
    $paths = @(Find-AllGamePaths -GameId 'snowrunner')
    if ($paths.Count -eq 0) { throw "SnowRunner not found. Pass -ExePath explicitly." }

    foreach ($path in $paths) {
        # SnowRunner.exe is nested under the install root, and a GDK build can
        # nest it somewhere else again - games.json carries both relpaths, so
        # neither is spelled out here.
        $isXbox = Test-IsXboxPath -Path $path -Config $config
        $relpath = if ($isXbox -and $config.ContainsKey('XboxExecutable') -and $config.XboxExecutable) {
            $config.XboxExecutable
        } else {
            $config.Executable
        }
        [pscustomobject]@{
            Path  = (Join-Path $path $relpath)
            Store = if ($isXbox) { 'gdk' } else { 'steam' }
        }
    }
}

function Get-PeFingerprint {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    # Every offset below is taken from the file, so each one is range-checked
    # before it is used. Without this a truncated download or a text file passed
    # by mistake comes back as a raw ArgumentException naming an array index,
    # which tells the person running this nothing about what they handed it.
    if ($bytes.Length -lt 0x40 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) {
        throw "Not a PE image (no MZ header): $Path"
    }
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset -gt ($bytes.Length - 4)) {
        throw "Not a PE image (PE header offset 0x{0:X} is outside the file): {1}" -f $peOffset, $Path
    }
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) { throw "Not a PE image: $Path" }

    $coff   = $peOffset + 4
    $optHdr = $coff + 20
    # SizeOfImage sits at optional-header +56 and CheckSum at +64, so the header
    # has to reach at least +68.
    if (($optHdr + 68) -gt $bytes.Length) {
        throw "PE optional header is truncated: $Path"
    }
    return [pscustomobject]@{
        TimeDateStamp = [BitConverter]::ToUInt32($bytes, $coff + 4)
        SizeOfImage   = [BitConverter]::ToUInt32($bytes, $optHdr + 56)
        CheckSum      = [BitConverter]::ToUInt32($bytes, $optHdr + 64)
    }
}

function Show-Target {
    param([Parameter(Mandatory)][pscustomobject]$Target)

    if (-not (Test-Path $Target.Path)) { throw "EXE not found: $($Target.Path)" }
    try {
        $pe = Get-PeFingerprint -Path $Target.Path
    } catch [System.Management.Automation.MethodInvocationException] {
        if ($_.Exception.InnerException -isnot [System.UnauthorizedAccessException]) { throw }
        # A Game Pass install keeps its content in a container only the gaming
        # services stack can open, so the exe is listable and not readable, by
        # any user, with any ACL. Nothing to fix - the fingerprint has to come
        # from the image as the loader maps it: run the game with the mod in
        # place and read the "[build] running EXE fingerprint" line out of
        # HeadTracking.log, which is the same three numbers this would print.
        Write-Host "EXE: $($Target.Path)"
        Write-Host "  store         $($Target.Store)"
        Write-Host "  This copy's exe cannot be read from disk (Game Pass keeps package" -ForegroundColor Yellow
        Write-Host "  content in a container). Launch it with the mod installed and read" -ForegroundColor Yellow
        Write-Host "  the [build] lines from HeadTracking.log in the game folder instead." -ForegroundColor Yellow
        return
    }
    $built = [DateTimeOffset]::FromUnixTimeSeconds($pe.TimeDateStamp).UtcDateTime

    Write-Host "EXE: $($Target.Path)"
    Write-Host "  store         $($Target.Store)"
    Write-Host ("  TimeDateStamp 0x{0:X8}  ({1:yyyy-MM-dd HH:mm:ss} UTC)" -f $pe.TimeDateStamp, $built)
    Write-Host ("  SizeOfImage   0x{0:X8}" -f $pe.SizeOfImage)
    Write-Host ("  CheckSum      0x{0:X8}" -f $pe.CheckSum)
    Write-Host ""

    $offsetsFile = Join-Path $projectDir "src/builds/$($Target.Store)_offsets.cpp"
    $matched = $false
    if (Test-Path $offsetsFile) {
        $known = Select-String -Path $offsetsFile -Pattern '\{\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8}),\s*0x([0-9A-Fa-f]{8})\s*\}'
        foreach ($k in $known) {
            $t = [Convert]::ToUInt32($k.Matches[0].Groups[1].Value, 16)
            $s = [Convert]::ToUInt32($k.Matches[0].Groups[2].Value, 16)
            $c = [Convert]::ToUInt32($k.Matches[0].Groups[3].Value, 16)
            if ($t -eq $pe.TimeDateStamp -and $s -eq $pe.SizeOfImage -and $c -eq $pe.CheckSum) {
                Write-Host "MATCH: this build already has a profile ($(Split-Path -Leaf $offsetsFile) line $($k.LineNumber))." -ForegroundColor Green
                $matched = $true
            }
        }
    }
    if ($matched) { return }

    # The offsets block is lifted verbatim out of the newest profile in the
    # store's own file rather than written out here. A hand-kept copy is what
    # this script shipped before, and it had drifted a whole design behind the
    # struct, so following it produced a profile that would not compile.
    #
    # A store with no file yet gets zeros instead. Seeding one store's template
    # with another store's RVAs would print numbers that look derived and are
    # measurements of a different binary.
    $offsetLines = @()
    $seededFrom = $null
    if (Test-Path $offsetsFile) {
        $offsetsText = Get-Content -Raw $offsetsFile
        # Name, then the fingerprint braces, then the offsets braces - the second
        # inner group is the one wanted, so the fingerprint's is matched
        # explicitly rather than skipped over.
        $blocks = [regex]::Matches($offsetsText,
            '(?s)extern\s+const\s+BuildProfile\s+\w+\s*=\s*\{[^{}]*\{[^{}]*\}\s*,\s*\{(.*?)\}\s*,\s*\}\s*;')
        if ($blocks.Count -eq 0) {
            throw "Could not read an existing profile's offsets block from $offsetsFile."
        }
        # The LAST block, not the first: the offsets files are append-only, so
        # the newest profile is at the bottom. Only kKnownProfiles in
        # build_registry.cpp is ordered newest-first. Seeding a rederive from the
        # oldest profile's RVAs starts it at a number that looks authoritative
        # and is several patches stale.
        $newest = $blocks[$blocks.Count - 1]
        $seededFrom = if ($newest.Value -match 'k\w+Profile_\d+') { $Matches[0] } else { 'the last profile in the file' }
        $offsetLines = $newest.Groups[1].Value -split "`n" |
            Where-Object { $_.Trim() } |
            ForEach-Object { '        ' + $_.Trim() }
    } else {
        $offsetLines = @('        0,  // every field of OffsetTable, in declaration order')
    }

    $constant = "k$((Get-Culture).TextInfo.ToTitleCase($Target.Store))Profile_$('{0:yyyyMMdd}' -f $built)"
    Write-Host "No profile matches this EXE. Append a new profile to src/builds/$($Target.Store)_offsets.cpp:" -ForegroundColor Yellow
    Write-Host ""
    Write-Host "extern const BuildProfile $constant = {"
    Write-Host ("    `"{0}-win64-{1:yyyyMMdd}`"," -f $Target.Store, $built)
    Write-Host ("    {{ 0x{0:X8}, 0x{1:X8}, 0x{2:X8} }}," -f $pe.TimeDateStamp, $pe.SizeOfImage, $pe.CheckSum)
    Write-Host "    {"
    $offsetLines | ForEach-Object { Write-Host $_ }
    Write-Host "    },"
    Write-Host "};"
    Write-Host ""
    Write-Host "Then add it to the TOP of kKnownProfiles in src/builds/build_registry.cpp."
    if ($seededFrom) {
        Write-Host ("Every number in that block is COPIED from {0}, not measured from this" -f $seededFrom)
        Write-Host "EXE. Rederive and confirm them before shipping. Until then zero the fields"
        Write-Host "IsProfileComplete() checks (build_profile.h names them): a zero in any of"
        Write-Host "them routes the build to a profile that stays dormant."
    } else {
        Write-Host "There is no profile file for this store yet, so there is nothing to copy:"
        Write-Host "every address has to be derived against THIS binary. A store variant is a"
        Write-Host "separate build, and another store's RVAs are measurements of a different"
        Write-Host "binary. Leaving the fields IsProfileComplete() checks at zero routes the"
        Write-Host "build to a profile that stays dormant, which is the safe state to land."
    }
}

$targets = @(Get-CheckTargets)
for ($i = 0; $i -lt $targets.Count; $i++) {
    if ($i -gt 0) { Write-Host "" }
    Show-Target -Target $targets[$i]
}
