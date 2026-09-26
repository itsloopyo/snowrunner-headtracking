# SnowRunner Head Tracking

![SnowRunner running with this mod](https://raw.githubusercontent.com/itsloopyo/snowrunner-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for SnowRunner that moves the camera with your head while your wheel or controller keeps steering, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and steering** - head tracking moves the camera; steering stays on your wheel or controller
- **6DOF positional tracking** - lean, peek and duck with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android
- **Field of view past the game's own limit** - one setting widens whichever view you are in, cabin or chase

## Requirements

- SnowRunner on [Steam](https://store.steampowered.com/app/1465360/SnowRunner/), or on Xbox Game Pass. Both are supported - see below.
- A tracking source that sends the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack) with a webcam
- Windows 10 or 11, 64-bit

### Which copies of the game this works on

The Steam build and the Xbox Game Pass build, both of the game
as patched on 2026-07-22.

Each store ships its own separately built exe, so the mod carries one entry per
build and picks between them from the running exe's own PE header. A build it
does not recognise - a patch it has not been updated for, or a store it has no
entry for - gets no hooks at all: the game runs vanilla and the log says which
way the mismatch went. Installing it on a copy it cannot help does not break
that copy.

Where the files go differs between the two, because the two lay their folders
out differently:

| Copy | Folder the two files go in |
|------|----------------------------|
| Steam | `<game>\Sources\Bin`, beside `SnowRunner.exe` |
| Xbox Game Pass | `<drive>:\XboxGames\SnowRunner - Windows10\Content`, beside `SnowRunner.exe` |

`install.cmd` finds both and puts them in the right place. Own it on both stores
and it installs into whichever one it finds first, so run it once per copy,
passing the path as an argument for the second.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **SnowRunner**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the `SnowRunnerHeadTracking-...-installer.zip` from the [releases page](https://github.com/itsloopyo/snowrunner-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1` port `4242`.
5. Launch the game.

The installer puts two files next to `SnowRunner.exe`: `SnowRunnerHeadTracking.asi` (the mod) and `dinput8.dll` (the bundled Ultimate ASI Loader, which the game already imports so the loader is picked up on start).

`SnowRunner.exe` is not at the top of the Steam game folder - it lives in `Sources\Bin`, and that is where both files go. The Xbox Game Pass copy keeps it at the root of `Content` instead. The loader only ever looks in the directory the exe is in, so a copy anywhere else does nothing at all.

Success looks like a `CameraUnlock.ini`, the mod's settings file, and a `HeadTracking.log` appearing beside `SnowRunner.exe` after the first launch, with the log reading `[build] activated profile steam-win64-20260722` (or `gdk-win64-20260722` on Xbox Game Pass) and a `[camera] hooked vehicle activity at ...` line.

If the installer cannot find your game, point it at the folder yourself, either with an environment variable:

```powershell
$env:SNOWRUNNER_PATH = "D:\Games\SnowRunner"
.\install.cmd
```

or by passing the path as an argument:

```powershell
.\install.cmd "D:\Games\SnowRunner"
```

On Steam, give it the folder that contains `Sources`, not the `Bin` folder itself. On Xbox Game Pass, give it the `Content` folder, which is the one `SnowRunner.exe` sits in.

### Manual Installation

Copy `plugins\SnowRunnerHeadTracking.asi` and `vendor\ultimate-asi-loader\dinput8.dll` out of the ZIP into the folder holding `SnowRunner.exe` - `<game>\Sources\Bin` on Steam, `<drive>:\XboxGames\SnowRunner - Windows10\Content` on Xbox Game Pass. The loader keeps its name; nothing needs renaming.

Mod managers do not deploy this mod. A manager installs into one fixed subtree of the game folder, and SnowRunner's own mod support goes through the in-game Mod Browser, which handles maps and trucks rather than files beside the exe. There is no Nexus archive for this mod for that reason - use `install.cmd`, or copy the two files by hand.

## Setting Up OpenTrack

1. Set **Input** to whatever tracker you use.
2. Set **Output** to `UDP over network`, host `127.0.0.1`, port `4242`.
3. Press **Start**.
4. Center with OpenTrack's own Center bind while sitting the way you drive.

### VR Headset Setup

1. Connect the headset over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on UDP `127.0.0.1` port `4242`.

### Webcam Setup

Set OpenTrack's **Input** to `Neuralnet tracker`. It tracks your face from a plain webcam, with no markers, clips or IR hardware to fit.

### Phone App Setup

The mod takes the OpenTrack UDP protocol and nothing else, so your app has to send that, either from the phone or through a companion program on the PC. Check your app's output settings for it first.

There are two ways to wire it up:

- **Straight to the game.** Point the app at your PC's local IP address, port `4242`.
- **Through OpenTrack.** Set OpenTrack's **Input** to your app, then follow [Setting Up OpenTrack](#setting-up-opentrack).

Try it straight to the game first. Hold your head still and watch: if the view shakes or creeps, your app is sending a rough feed, and putting OpenTrack in the middle will clean it up. Use OpenTrack anyway if you want its curves.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It smooths on the phone, so it goes straight to the game.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action              | Nav-cluster | Chord           |
|---------------------|-------------|-----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y`  |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G`  |
| Toggle yaw mode     | `Page Down` | `Ctrl+Shift+H`  |

Both columns fire the same action. Each action's keys are one list in `[Hotkeys]` in `CameraUnlock.ini`, `ToggleKey`, `CycleTrackingModeKey` and `YawModeKey`, the chord included, so any of them can be changed or removed.

`Page Up` / `Ctrl+Shift+G` cycles tracking mode:

1. Normal head-tracked gameplay
2. Positional tracking disabled, rotational tracking enabled
3. Rotational tracking disabled, positional tracking enabled
4. Back to normal

`Page Down` / `Ctrl+Shift+H` switches what your head turns the view about. World up, the default, keeps the horizon level when the chase camera looks down at the truck. The camera's own up axis keeps the view glued to the truck through a side slope, a roll down a bank, or a landing on the roof.

The tracking mode and the yaw mode you pick are saved to `CameraUnlock.ini`, and the game starts in them next time. `End` / `Ctrl+Shift+Y` changes the current session only; whether tracking is on when the game starts is `EnableOnStartup`.

Centering is done in the tracker: OpenTrack's Center bind, the center button in your phone app, or SteamVR's reset.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, at one of these paths depending on the store the game came from:

- `Sources\Bin\CameraUnlock.ini`
- `CameraUnlock.ini`

It creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.

A setting that the defaults below set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it. `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.

Comments, and keys the mod never read, are not carried over. Nor are these, where your old file had them:

- Reticle settings, and a key that toggled the reticle.
- A sensitivity, scale, deadzone, response curve or axis inversion you changed from its default. Set these in your tracker instead.
- The setting for a feature that earlier versions shipped switched off while it was untested. It now follows the mod's default.

An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.

Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults below. Every setting they set to `default` then follows `Defaults.ini`.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`

With every setting at its default, the file reads:

```ini
; SnowRunner head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true: yaw turns around the world's up axis. false: around the camera's own up axis.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default

[Camera]
; The angle the view spans across the width of the screen, in degrees: 0, or 30 to 140.
; 0 keeps the game's own Field of View settings. The cabin and the chase view both
; render at this angle while it is set. SnowRunner's sliders use a different scale,
; so the same number gives a different view. HeadTracking.log names the angle the
; game was drawing. Turning your head ten degrees turns the view ten degrees at
; every setting, and the setting stays applied while head tracking is off.
Fov=0.0
```
<!-- /cameraunlock:config -->

Changes take effect the next time the game starts.

Hotkeys are written as key names, such as `End`, `PageUp`, `F9` or `Ctrl+Shift+Y`, separated by commas. A key with no name can be written as its Windows virtual key code, `0x` and two hex digits, such as `0xBA`. A value the mod cannot read leaves that setting at its default and is named in `HeadTracking.log`.

`[Camera] Fov` sets the actual horizontal viewing angle in degrees, from 30 to 140. For example, `Fov=130` spans 130 degrees across the screen in both cabin and chase views. `Fov=0` keeps the game's own Field of View settings.

SnowRunner's sliders use a different scale. The game multiplies the slider value by 9/16 to get a base vertical angle, then applies further camera-state adjustments. Its 130 therefore starts at 73.125 degrees vertically, before those adjustments. Matching numbers in the game and the mod do not give matching views.

The mod keeps the horizontal angle fixed at your chosen value. The vertical angle follows the screen's aspect ratio: at the same `Fov`, an ultrawide screen shows less vertically than a 16:9 screen. The override stays applied while head tracking is toggled off.

`HeadTracking.log` reports the game's horizontal and vertical angles before the override on a line starting `[camera]`, followed by the configured result when `Fov` is set. This samples the first reported view; the game's angles can change with its settings, camera state and aspect ratio.

The mod picks between the two smoothing values by where the packets came from, and it goes by address rather than by machine. Any `127.x.x.x` address counts as local. A phone on your WiFi gets `RemoteSmoothing`, which is what you want, but so does OpenTrack running on this same PC if you have pointed it at your PC's own network address. Send to a loopback address to get `LocalSmoothing`.

## Troubleshooting

Read `HeadTracking.log`, next to `SnowRunner.exe`. It records the game folder, the build profile it matched or refused, the config it loaded, the camera update it hooked, and the first head pose that reached the camera.

**Mod not loading:**

- No log file at all means the loader is not being picked up. Check that `dinput8.dll` and `SnowRunnerHeadTracking.asi` are both beside `SnowRunner.exe` - `Sources\Bin` on Steam, `Content` on Xbox Game Pass - and not in the folder above it.
- If the log says the mod stayed dormant, your `SnowRunner.exe` is not a build this mod has a profile for. The mod fingerprints the running exe (TimeDateStamp, SizeOfImage and CheckSum) and installs no hooks unless it matches. The log line says whether your build is newer or older than the ones it knows; a newer one needs a mod update.

**No tracking response:**

- Check the log for `head pose reached the camera hook`. If it is absent the packets are not arriving: confirm the tracker's output host and port match `UdpPort`, and that no firewall rule is dropping them.
- If it is present, press `End` (or `Ctrl+Shift+Y`). Tracking may be toggled off.
- Only one process can hold UDP 4242. If OpenTrack or another game with a head tracking mod already has it, the log says so and keeps retrying twice a second. Close the other app and the mod picks the port up within about half a second. No restart needed.
- No movement in a menu, on a loading screen or with the game paused is the mod holding off on purpose: the camera function it hooks is not called there at all.
- The mod watches SnowRunner's own network session and holds the camera still while one is live, so head tracking is meant to stay off in co-op and come back when you are driving on your own. That gate has only been exercised in single player, where it reports "not running" throughout. The log line starting `[state] co-op session` says which of the two it is seeing - if it reads LIVE while you are driving alone, or head tracking follows your head in a co-op session, please say so on Discord.

**Jittery or unstable tracking:**

- Raise `RemoteSmoothing` if the tracker is on another device, or `LocalSmoothing` if it is on this PC, both in `[Smoothing]` in `CameraUnlock.ini`. Both are frame-rate independent.
- If a phone app is sending direct, route it through OpenTrack so its filters can clean up the feed.

**Wrong rotation axis:**

- The mod applies the pose as your tracker sends it. If an axis moves the wrong way, invert that axis in your tracker's settings.

**Edits to `CameraUnlock.ini` do nothing:** the file is read once at startup, so restart the game. If a single value is being ignored, the log names it. Once `CameraUnlock.ini` exists the mod no longer reads `HeadTracking.ini`, so an edit there changes nothing.

## Updating

Download the new release and run `install.cmd` again. Your `CameraUnlock.ini` is kept. Updating from v0.2.0 or earlier, the first start reads your settings from `HeadTracking.ini` into a new `CameraUnlock.ini`; see [Configuration](#configuration).

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway. `CameraUnlock.ini` and `HeadTracking.ini` are left in place either way, so your settings are still there if you install again.

## Building from Source

Needs [pixi](https://pixi.sh) and Visual Studio with the C++ toolchain.

```powershell
git clone --recursive https://github.com/itsloopyo/snowrunner-headtracking.git
cd snowrunner-headtracking
pixi run build      # SnowRunnerHeadTracking.asi
pixi run test       # unit tests
pixi run package    # the release ZIP
```

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

The loader it ships and the libraries compiled into the `.asi` keep their own licenses, listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md), which travels at the root of the release ZIP. Copies also sit under `licenses/` and beside the vendored loader.

## Credits

- SnowRunner by [Saber Interactive](https://saber.games/), published by Focus Entertainment
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu
- [OpenTrack](https://github.com/opentrack/opentrack)
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core), the shared head tracking pipeline behind these mods

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Saber Interactive or Focus Entertainment. Use at your own risk.
