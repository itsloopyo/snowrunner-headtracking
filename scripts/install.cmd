@echo off
:: ============================================
:: SnowRunner Head Tracking - Install
:: ============================================
:: Thin wrapper - install body lives in cameraunlock-core/scripts/install-body-asi.cmd,
:: staged into the release ZIP's shared/ by Copy-SharedBundle. To change
:: install behaviour edit the body, not this wrapper.
::
:: Source of truth for everything below the CONFIG BLOCK:
:: cameraunlock-core/scripts/templates/install-wrapper-asi.cmd. Copy this
:: file to <mod>/scripts/install.cmd, fill in the CONFIG BLOCK, change nothing
:: else. scripts/conformance.ps1 checks that nothing else changed.
::
:: Ultimate ASI Loader: one DLL renamed to the proxy the game already
:: imports, with the mod shipped as an .asi beside the game exe. Check the
:: exe's import table before choosing ASI_LOADER_NAME - a proxy the game does
:: not import is never loaded and the mod silently does nothing.
:: ============================================

:: --- CONFIG BLOCK ---
set "GAME_ID=snowrunner"
set "MOD_DISPLAY_NAME=SnowRunner Head Tracking"
set "MOD_DLLS=SnowRunnerHeadTracking.asi"
set "MOD_INTERNAL_NAME=SnowRunnerHeadTracking"
set "MOD_VERSION=0.0.0"
set "STATE_FILE=.headtracking-state.json"
set "FRAMEWORK_TYPE=ASILoader"
:: Filename the ASI loader DLL is renamed to: the import the game exe already
:: has. SnowRunner.exe imports DINPUT8.dll, so the loader takes that name and
:: the game-local copy wins over the system one. Matches launcher-manifest.json,
:: which deploys the same vendored dinput8.dll to this name.
set "ASI_LOADER_NAME=dinput8.dll"
:: Left empty. SnowRunner.exe is at Sources\Bin\SnowRunner.exe rather than the
:: install root, and the payload does have to sit beside it - but the install
:: body already descends into the exe's own directory from games.json's
:: executable_relpath, so naming the subdirectory again doubles it.
set "ASI_SUBDIR="
:: The release ZIP ships no HeadTracking.ini: the mod writes its own with the
:: documented defaults on first run, so there is nothing to seed write-if-absent.
set "MOD_SEED_FILES="
:: Left empty so the state file omits framework.version. Nothing here or in core
:: rewrites this line when `pixi run update-deps` bumps vendor/, so a value set
:: now would keep reporting the old loader after the next bump.
set "ASI_LOADER_VERSION="
:: Post-install help text. `&echo ` starts each further line.
set "MOD_CONTROLS=Controls:&echo   End  / Ctrl+Shift+Y - Toggle head tracking on/off&echo   PgUp / Ctrl+Shift+G - Cycle tracking mode (rotation and position / rotation only / position only)&echo.&echo Both are remappable in HeadTracking.ini, written to the game folder on first run."
:: --- END CONFIG BLOCK ---

:: Pin delayed expansion off before `%*` is expanded on the `call` below.
:: Under `cmd /V:ON`, or with DelayedExpansion=1 in
:: HKCU\Software\Microsoft\Command Processor, cmd.exe eats a `!` out of the
:: expanded line, and a real game path like C:\Games\Oh! My Game reaches the
:: body already mangled. The body pins expansion off at its own outer scope
:: too, but that is one `call` too late to save the argument it was handed.
setlocal disabledelayedexpansion

set "WRAPPER_DIR=%~dp0"
set "_BODY=%WRAPPER_DIR%shared\install-body-asi.cmd"
if not exist "%_BODY%" set "_BODY=%WRAPPER_DIR%..\cameraunlock-core\scripts\install-body-asi.cmd"
if not exist "%_BODY%" (
    echo ERROR: install-body-asi.cmd not found in shared\ or ..\cameraunlock-core\scripts\.
    echo If this is a release ZIP, re-download it from GitHub ^(corrupt installer^).
    echo If this is the dev tree, run: git submodule update --init --recursive
    exit /b 1
)
call "%_BODY%" %*
exit /b %errorlevel%
