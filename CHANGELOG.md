# Changelog

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
