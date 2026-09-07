#Requires -Version 5.1
# Fully unattended. `pixi run release <major|minor|patch|nightly|X.Y.Z>` is the
# authorization - there is no confirmation gate. Preconditions (on main, clean
# tree, tag absent, valid semver) are the safety net; any failure exits
# non-zero with a one-line diagnostic.
param(
    [string]$Version,
    # Ship a release even when there are no user-facing commits since the last
    # tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$projectDir = Split-Path -Parent $PSScriptRoot
Import-Module (Join-Path $projectDir "cameraunlock-core/powershell/ReleaseWorkflow.psm1") -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
# UTF-8 with no BOM, always. Windows PowerShell 5.1's Set-Content defaults to
# the system ANSI codepage, and Get-Content -Raw assumes the same for a file
# with no BOM - so a commit subject carrying an accent or a typographic
# apostrophe round-trips into mojibake in CHANGELOG.md, which is then copied
# verbatim into the release ZIP. package-release.ps1 writes through .NET for
# this reason; so does this.
function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw -Encoding UTF8
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Write-Utf8NoBom $Path $changelog
}

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Error "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>"
    exit 1
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

$pixiPath       = Join-Path $projectDir "pixi.toml"
$changelogPath  = Join-Path $projectDir "CHANGELOG.md"
$installCmdPath = Join-Path $projectDir "scripts/install.cmd"
$cmakePath      = Join-Path $projectDir "CMakeLists.txt"
$manifestPath   = Join-Path $projectDir "launcher-manifest.json"

# 1. Resolve + validate version against the canonical source (pixi.toml).
$pixiContent = Get-Content $pixiPath -Raw -Encoding UTF8
if ($pixiContent -notmatch '(?m)^version\s*=\s*"([^"]+)"') {
    throw "No version field found in $pixiPath"
}
$currentVersion = $matches[1]
$newVersion = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
if (-not (Test-SemanticVersion -Version $newVersion)) {
    throw "Resolved version '$newVersion' is not valid semver (X.Y.Z)."
}
Write-Host "Releasing v$newVersion (current v$currentVersion)" -ForegroundColor Cyan

# 2. Preconditions - fail fast, never prompt.
$branch = (git rev-parse --abbrev-ref HEAD).Trim()
if ($branch -ne "main") { throw "Releases must run on 'main' (currently on '$branch')." }
if (-not (Test-CleanGitStatus)) { throw "Working tree is not clean. Commit or stash first." }
if (Test-GitTagExists -Tag "v$newVersion") { throw "Tag v$newVersion already exists." }

# THIRD-PARTY-NOTICES.md names the cameraunlock-core commit compiled into the
# release ZIPs, and bumping the submodule does not touch it. Packaging refuses
# to ship that mismatch, so a bump with no notices edit stopped the release
# here, or in CI once the tag had already been pushed. Re-sync it and let this
# release carry the correction.
#
# This runs AFTER the preconditions above, not before them: it is the first
# step that writes to the repo, and it used to commit on `main` before the
# script had established it was on main, that the tree was clean, or that the
# version argument was even valid - including on the `nightly` path, which
# never gets this far now.
& (Join-Path $projectDir 'cameraunlock-core\scripts\sync-core-notices.ps1') -Repo $projectDir
if ($LASTEXITCODE -ne 0) { throw "sync-core-notices.ps1 exited $LASTEXITCODE - fix THIRD-PARTY-NOTICES.md before releasing." }
& git -C $projectDir diff --quiet -- THIRD-PARTY-NOTICES.md
if ($LASTEXITCODE -ne 0) {
    & git -C $projectDir commit -q -m 'chore: record the cameraunlock-core commit this build compiles' -- THIRD-PARTY-NOTICES.md
    if ($LASTEXITCODE -ne 0) { throw "Could not commit the re-synced THIRD-PARTY-NOTICES.md." }
    Write-Host 'THIRD-PARTY-NOTICES.md re-synced to the pinned cameraunlock-core commit.' -ForegroundColor Yellow
}

# 3. Generate the changelog from commits since the last tag. This is the gate
#    that aborts when there are no user-facing commits, so run it BEFORE
#    mutating any version files or building - a failure here then leaves a
#    clean tree instead of stranding a half-applied version bump with no tag.
Write-Host "Generating CHANGELOG..." -ForegroundColor Cyan
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    if (-not (Test-Path $changelogPath)) {
        $date = Get-Date -Format 'yyyy-MM-dd'
        Write-Utf8NoBom $changelogPath "# Changelog`n`n## [$newVersion] - $date`n`nFirst release.`n"
        Write-Host "  Wrote initial CHANGELOG.md" -ForegroundColor Gray
    }
} else {
    try {
        New-ChangelogFromCommits -ChangelogPath $changelogPath -Version $newVersion | Out-Null
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $newVersion
    }
}

# 4. Bump the canonical version, then mirror it into every derived copy.
$pixiContent = $pixiContent -replace '(?m)^(version\s*=\s*")[^"]+(")', "`${1}$newVersion`${2}"
Write-Utf8NoBom $pixiPath $pixiContent

$cmakeContent = Get-Content $cmakePath -Raw -Encoding UTF8
if ($cmakeContent -notmatch 'project\(SnowRunnerHeadTracking VERSION [0-9]+\.[0-9]+\.[0-9]+') {
    throw "No project VERSION found in $cmakePath"
}
$cmakeContent = $cmakeContent -replace '(project\(SnowRunnerHeadTracking VERSION )[0-9]+\.[0-9]+\.[0-9]+', "`${1}$newVersion"
Write-Utf8NoBom $cmakePath $cmakeContent

# -Raw + a digits-only regex keeps install.cmd's CRLF endings intact (a .cmd
# silently fails on Windows if rewritten LF).
$installContent = Get-Content $installCmdPath -Raw -Encoding UTF8
if ($installContent -notmatch '(?m)^set "MOD_VERSION=[0-9]+\.[0-9]+\.[0-9]+"') {
    throw "No MOD_VERSION line found in $installCmdPath"
}
$installContent = $installContent -replace '(?m)^(set "MOD_VERSION=)[0-9]+\.[0-9]+\.[0-9]+(")', "`${1}$newVersion`${2}"
Write-Utf8NoBom $installCmdPath $installContent

$manifestContent = Get-Content $manifestPath -Raw -Encoding UTF8
if ($manifestContent -notmatch '(?m)^\s*"version"\s*:\s*"[0-9]+\.[0-9]+\.[0-9]+"') {
    throw "No version field found in $manifestPath"
}
$manifestContent = $manifestContent -replace '(?m)^(\s*"version"\s*:\s*")[0-9]+\.[0-9]+\.[0-9]+(")', "`${1}$newVersion`${2}"
Write-Utf8NoBom $manifestPath $manifestContent

# 5. The same gates CI runs, in the same order, BEFORE anything is committed
#    or tagged. `pixi run package` is what carries the release gates - the
#    vendored-loader SHA-256 check, the vendor and licence presence checks and
#    Assert-CoreCommitInNotices - and it depends on `build`, so this is a
#    superset of compiling. Running only `pixi run build` here meant a release
#    could be tagged and pushed and then fail in CI at packaging, stranding the
#    tag and the version-bump commit on main with no GitHub Release behind them.
Write-Host "Running tests..." -ForegroundColor Cyan
pixi run test
if ($LASTEXITCODE -ne 0) { throw "Tests failed; aborting release." }

Write-Host "Building and packaging release..." -ForegroundColor Cyan
pixi run package
if ($LASTEXITCODE -ne 0) { throw "Packaging failed; aborting release." }

# 6. Commit the version bump + changelog. "Release v..." matches the build.yml
#    skip guard so CI doesn't double-build this commit.
git add $pixiPath $changelogPath $installCmdPath $manifestPath $cmakePath
if ($LASTEXITCODE -ne 0) { throw "git add failed." }
git commit -m "Release v$newVersion"
if ($LASTEXITCODE -ne 0) { throw "git commit failed." }

# 7 + 8. Annotated tag, then push commits + tag (triggers release.yml).
New-ReleaseTag -Version $newVersion -Message "Release v$newVersion" -Branch "main"

Write-Host "Released v$newVersion. CI release workflow will publish the ZIPs." -ForegroundColor Green
