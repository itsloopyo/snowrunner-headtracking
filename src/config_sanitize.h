// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>

namespace sr_ht {

// Boundary validation for values read from the user-editable HeadTracking.ini.
// IniReader parses floats with strtod, which accepts "nan" and "inf" and
// overflows a literal like 1e400 to +inf, so a typo or a corrupted file feeds
// those straight into the smoothing math, the quaternion, and from there into
// the camera transform this mod writes back into the engine. Every float that
// crosses that boundary goes through one of these first.

inline float SanitizeFinite(float v, float fallback) {
    return std::isfinite(v) ? v : fallback;
}

inline float ClampRange(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// LocalSmoothing and RemoteSmoothing must each be finite and within [0,1].
// [0,1] is the whole meaningful domain: CalculateSmoothingFactor maps it onto a
// settle speed between 50 (a 20ms time constant, so frame interpolation is what
// is left) and 0.1 (a 10s time constant), and the core clamps that speed to
// [0.1, 50] itself, so a value outside the range no longer drives the per-frame
// factor negative. It
// just saturates at one end while the INI goes on advertising a setting the mod
// is not honouring, so the clamp stays: it keeps the stored value and the
// behaviour in agreement, and gives the caller something to log.
//
// This is validation, never a floor. Any value inside [0,1] reaches the
// processor untouched, 0.0 included. `fallback` is the shipped default of the
// key being read, 0.0 for LocalSmoothing and 0.15 for RemoteSmoothing, so a
// malformed RemoteSmoothing lands on the remote default instead of silently
// handing a phone-over-WiFi user the local "no smoothing at all".
inline float SanitizeSmoothing(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, 1.0f);
}

// Sensitivity is a magnitude, never a sign. A negative value used to be
// accepted and passed through, which made it a second, undocumented way to
// invert an axis sitting alongside the Invert flags - two mechanisms for one
// job, neither mentioned in the INI, and a -1 reads as a typo rather than an
// intent. Negatives now clamp to zero and are reported like any other refused
// value, which points the user at the Invert flag they meant.
//
// The upper bound refuses the value that reaches the camera matrix as garbage:
// a magnitude large enough that TrackingProcessor's `angle * sensitivity`
// overflows to +/-Inf. sin/cos of an infinite angle is NaN, so that lands a NaN
// camera transform in the engine every frame - a black screen with nothing in
// the log to explain it. The angle being multiplied is a quaternion
// decomposition and so never exceeds 180 degrees, which this bound keeps finite
// with room to spare while sitting well beyond any usable setting.
constexpr float kMaxSensitivity = 100.0f;

inline float SanitizeSensitivity(float v) {
    // A negative lands on the default, not on zero. Clamping it to the bottom of
    // the range froze the axis completely, which is a worse answer to a typo
    // than the one every other refused value in this layer gets.
    if (v < 0.0f) return 1.0f;
    return ClampRange(SanitizeFinite(v, 1.0f), 0.0f, kMaxSensitivity);
}

// FovScale multiplies the frustum's half-extent, so the bounds are the range
// over which the result is still a picture of the world rather than a fisheye
// or a keyhole. What each end means depends on where the player has SnowRunner's
// own Field of View settings, because this scales the angle those produce rather
// than replacing it.
//
// The clamp also keeps the tangent bounded. Nothing in this range approaches the
// 180 degrees at which a perspective projection stops existing, so a mistyped
// 100 lands on a wide view instead of a projection matrix full of Inf - and a
// negative one, which would mirror the frame, lands on the narrow end.
constexpr float kMinFovScale = 0.5f;
constexpr float kMaxFovScale = 2.0f;

inline float SanitizeFovScale(float v) {
    return ClampRange(SanitizeFinite(v, 1.0f), kMinFovScale, kMaxFovScale);
}

// A virtual key code the hotkey poller can actually watch. GetAsyncKeyState
// only defines 0x01..0xFE, so a typo like ToggleKey=0x230 registers a hotkey
// that can never fire and the key silently does nothing.
//
// The modifiers are refused for a second reason: Ctrl and Shift are what the
// chord guard tests, so an action bound to one either never fires (a nav
// binding is suppressed while the chord is held) or fires on every press of
// any chord. Alt sits with them because it is the same class of key and a
// binding on it reads as a modifier the user expects to combine, not press.
inline bool IsBindableVirtualKey(int v) {
    if (v < 0x01 || v > 0xFE) return false;
    // 0x01-0x06 are the five mouse buttons plus VK_CANCEL (0x03, Ctrl+Break),
    // and GetAsyncKeyState reports all of them exactly like keys. `ToggleKey=1`
    // is a plausible reach for the "1" key - whose code is actually 0x31 - and
    // binding it would toggle head tracking on every left click; 0x02 is right
    // mouse, which is camera look.
    if (v <= 0x06) return false;
    if (v >= 0x10 && v <= 0x12) return false;  // Shift, Control, Alt
    if (v >= 0xA0 && v <= 0xA5) return false;  // and their left/right halves
    return true;
}

// Travel limits in metres. PositionProcessor clamps each axis to
// [-limit, +limit], so a negative limit inverts the clamp bounds and a
// non-finite one propagates NaN into the camera translation.
//
// The upper bound is the documented maximum for these keys rather than a
// generous headroom figure. It used to be 10 m, justified as catching a
// mistyped 10000 - but 10 m already puts the eye outside the cab and through
// the terrain, so the bound was refusing only the typos that did not matter.
// A head in a truck cab has centimetres of travel, and this mod has no lean
// clamp against level geometry, so nothing downstream would catch an accepted
// 10 m either. `LimitZ=40` from someone thinking in centimetres now lands on
// the documented 0.5 and says so.
constexpr float kMaxPositionLimit = 0.5f;

inline float SanitizePositionLimit(float v, float fallback) {
    return ClampRange(SanitizeFinite(v, fallback), 0.0f, kMaxPositionLimit);
}

}  // namespace sr_ht
