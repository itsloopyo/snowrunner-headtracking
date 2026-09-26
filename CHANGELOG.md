# Changelog

## [Unreleased]

### Added

- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed

- Settings move to `CameraUnlock.ini`, next to `SnowRunner.exe`: `Sources\Bin\CameraUnlock.ini` on Steam, and `CameraUnlock.ini` in the `Content` folder on Xbox Game Pass. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A first start with no `HeadTracking.ini` no longer writes one. It creates `CameraUnlock.ini` instead.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor is this, where your old file had it:
  - A sensitivity or axis inversion you changed from its default. Set these in your tracker instead.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] ToggleKey` and `ChordToggleKey`, virtual key codes, are imported together into `ToggleKey`, `CycleModeKey` and `ChordCycleModeKey` into `CycleTrackingModeKey`, and `YawModeKey` and `ChordYawModeKey` into `YawModeKey`, each as the plain key and Ctrl+Shift with the chord key.
- The tracking mode (Page Up / Ctrl+Shift+G) and the yaw mode (Page Down / Ctrl+Shift+H) are saved to `CameraUnlock.ini` when you change them, and the game starts in the modes you left it in. The yaw mode is `WorldSpaceYaw`, and every earlier version started with it on, so an imported file starts that way too. Turning tracking on or off (End / Ctrl+Shift+Y) is still not saved; the game starts with head tracking on or off as `EnableOnStartup` says.
- The keys are renamed to the names every head tracking mod on `CameraUnlock.ini` uses: `LocalSmoothing` and `RemoteSmoothing` move from `[Rotation]` to `[Smoothing]`, and the lean limits are `PositionLimitX`, `PositionLimitZ` and `PositionLimitZBack`. `LimitY` set both vertical limits, so it becomes `PositionLimitY` (up) and `PositionLimitYDown` (down), both imported from it. `[Position] Enabled`, which chose the mode tracking started in, is imported as that mode: `Enabled=0` starts in rotation only, as it did. `[Camera] Fov` keeps its name.
- A value in `CameraUnlock.ini` that is outside a setting's range is no longer pulled to the nearest value the mod accepts. The setting keeps its default and the log names the line. `Fov` takes 0 or 30 to 140, `UdpPort` 1 to 65535, and the lean limits 0 to 10 metres, where `HeadTracking.ini` held them to 0.5. The import carries every value exactly as earlier versions used it.

### Removed

- The sensitivity and axis inversion settings, `[Rotation] YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch` and `InvertRoll`, and `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`, `InvertX`, `InvertY` and `InvertZ`. Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.

## [0.2.0] - 2026-09-11

### Added

- add a FovScale setting that widens the view past the game's own limit
- set the field of view in degrees instead of as a multiplier
- support the PC Game Pass build

### Fixed

- stop scenery vanishing at the edges of a head-turned or widened view
- stop a white glare column sliding in from the edge opposite a head turn
- turn the view rays and visibility bounds with the head

## [0.0.0] - 2026-09-06

### Added
- Added six degrees of freedom head tracking for the SnowRunner drive camera,
  so the view moves with your head while the wheel and pedals keep driving the
  truck. The truck's follow spring and camera shake behave exactly as they do
  with tracking off.
- Added camera-local rotation on all three axes, with no world-locked yaw mode.
  A SnowRunner truck spends real time on two wheels and some of it upside down,
  and a world-locked yaw axis in those moments turns the view about something
  with no relation to where the driver is looking.
- Added suppression of head tracking outside gameplay, so the view holds still
  in the menus, on a loading screen and while the game is paused.
- Added a co-op gate that holds the camera still while a network session is
  live. Confirmed silent in single player; not yet exercised in a live
  session.
- Added centering of a windowed game on the work area of the monitor it opened
  on. A fullscreen or borderless window, and one the game centred itself, are
  left where they are.
- Added hotkeys: `End` (or `Ctrl+Shift+Y`) toggles tracking, `Page Up` (or
  `Ctrl+Shift+G`) cycles between full tracking, rotation only and position
  only. All four keys are remappable.
- Added `HeadTracking.ini`, written next to `SnowRunner.exe` on first run with
  every setting and its default, and read at startup. A value the mod cannot
  use is named in the log along with what it used instead, and so is a key it
  does not recognise.
- Added reclaiming the tracker port: if another app is holding UDP 4242 when
  the game starts, the mod picks it up about half a second after that app
  releases it, with no restart.
- Added a per-build check on `SnowRunner.exe`. On a build the mod does not
  know it installs nothing at all and the game runs exactly as it would
  without the mod, with the reason in the log.
