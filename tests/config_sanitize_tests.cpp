// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Boundary tests for the values HeadTracking.ini feeds into the camera path.
// Pure functions only - no game, no sockets, no Windows API - so this runs
// anywhere `pixi run test` runs.

#include "config_sanitize.h"

#include "test_support.h"

#include "cameraunlock/protocol/port_utils.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace sr_ht;
using sr_test::Check;

namespace {

const float kNan = std::numeric_limits<float>::quiet_NaN();
const float kInf = std::numeric_limits<float>::infinity();
const float kFloatMax = std::numeric_limits<float>::max();

// The shipped default of each smoothing key. They are not the same number, and
// that is the whole point of the fallback argument.
const float kLocalDefault  = 0.0f;
const float kRemoteDefault = 0.15f;

void SmoothingTests() {
    std::printf("SanitizeSmoothing\n");
    Check(SanitizeSmoothing(0.0f, kLocalDefault) == 0.0f, "0 passes through");
    Check(SanitizeSmoothing(0.15f, kLocalDefault) == 0.15f, "in-range passes through");
    Check(SanitizeSmoothing(1.0f, kLocalDefault) == 1.0f, "1 passes through");

    // A configured zero is a real setting - track me with no added latency -
    // and it has to reach the processor as written. Nothing here may raise it,
    // least of all on the remote key, whose fallback is 0.15.
    Check(SanitizeSmoothing(0.0f, kRemoteDefault) == 0.0f,
          "a configured 0 survives verbatim on the remote key, never floored to 0.15");

    // Out of range saturates. Not because the math breaks - the core clamps its
    // own interpolation speed to [0.1, 50], so a smoothing above 1 no longer
    // drives the per-frame factor negative - but so the value the mod acts on
    // and the value the INI advertises stay the same number.
    Check(SanitizeSmoothing(5.0f, kLocalDefault) == 1.0f, "above 1 clamps to 1");
    Check(SanitizeSmoothing(-2.0f, kRemoteDefault) == 0.0f,
          "below 0 clamps to the bound, not to the fallback");

    // Non-finite values take the fallback, and it is the fallback of the key
    // that was read. A malformed RemoteSmoothing answered with the LOCAL
    // default would hand a phone on WiFi zero smoothing on raw network jitter,
    // which is the one case RemoteSmoothing exists to cover.
    Check(SanitizeSmoothing(kNan, kLocalDefault) == 0.0f,
          "a NaN LocalSmoothing falls back to the local default 0.0");
    Check(SanitizeSmoothing(kNan, kRemoteDefault) == 0.15f,
          "a NaN RemoteSmoothing falls back to the remote default 0.15, not to 0.0");
    Check(SanitizeSmoothing(kInf, kRemoteDefault) == 0.15f,
          "an Inf RemoteSmoothing falls back to the remote default 0.15");
    Check(SanitizeSmoothing(-kInf, kRemoteDefault) == 0.15f,
          "a -Inf RemoteSmoothing falls back to the remote default 0.15");
    Check(SanitizeSmoothing(kInf, kLocalDefault) == 0.0f,
          "an Inf LocalSmoothing falls back to the local default 0.0");
    Check(SanitizeSmoothing(-kInf, kLocalDefault) == 0.0f,
          "a -Inf LocalSmoothing falls back to the local default 0.0");
}

void SensitivityTests() {
    std::printf("SanitizeSensitivity\n");
    Check(SanitizeSensitivity(1.0f) == 1.0f, "default passes through");
    Check(SanitizeSensitivity(2.5f) == 2.5f, "boost preserved");
    Check(SanitizeSensitivity(0.0f) == 0.0f, "zero preserved");

    // A negative used to pass straight through, which made the sign a second
    // way to invert an axis sitting beside the Invert flags - undocumented, and
    // a -1 reads as a typo rather than an intent. It lands on the DEFAULT, not
    // on zero: clamping to the bottom of the range would freeze the axis, which
    // is a worse answer to a typo than the one every other refusal gives.
    Check(SanitizeSensitivity(-1.0f) == 1.0f, "a negative falls back to the default, not to zero");
    Check(SanitizeSensitivity(-0.001f) == 1.0f, "however small the negative");
    Check(SanitizeSensitivity(kNan) == 1.0f, "NaN -> 1");
    Check(SanitizeSensitivity(kInf) == 1.0f, "Inf -> 1");
    Check(SanitizeSensitivity(-kInf) == 1.0f, "-Inf -> 1");

    // A finite but enormous multiplier overflows the processor's
    // `angle * sensitivity` to Inf, and sin/cos of that is NaN - which is a NaN
    // camera transform handed to the engine on every frame.
    Check(SanitizeSensitivity(kFloatMax) == 100.0f, "absurd magnitude clamps");
    Check(SanitizeSensitivity(-kFloatMax) == 1.0f, "and an absurd negative lands on the default");
    Check(std::isfinite(SanitizeSensitivity(kFloatMax) * 180.0f),
          "the clamp keeps a full-range angle finite once scaled");
}

void PositionLimitTests() {
    std::printf("SanitizePositionLimit\n");
    Check(SanitizePositionLimit(0.30f, 0.30f) == 0.30f, "default passes through");
    Check(SanitizePositionLimit(0.50f, 0.30f) == 0.50f, "the documented maximum passes through");
    Check(SanitizePositionLimit(0.0f, 0.30f) == 0.0f, "zero (axis pinned) preserved");
    // A negative limit inverts PositionProcessor's clamp bounds.
    Check(SanitizePositionLimit(-0.5f, 0.30f) == 0.0f, "negative -> 0");
    Check(SanitizePositionLimit(kNan, 0.30f) == 0.30f, "NaN -> fallback");
    Check(SanitizePositionLimit(kInf, 0.20f) == 0.20f, "Inf -> fallback");
    Check(SanitizePositionLimit(-kInf, 0.40f) == 0.40f, "-Inf -> fallback");

    // The bound is the documented maximum, not a headroom figure. It used to be
    // 10 m, which already puts the eye outside the cab and through the terrain -
    // so it refused only the typos that did not matter. `LimitZ=40` from someone
    // thinking in centimetres is the one that does.
    Check(SanitizePositionLimit(40.0f, 0.40f) == 0.5f, "centimetres-for-metres clamps to 0.5");
    Check(SanitizePositionLimit(1.25f, 0.30f) == 0.5f, "a metre-plus lean clamps to 0.5");
    Check(SanitizePositionLimit(10000.0f, 0.30f) == 0.5f, "absurd limit clamps");
    Check(SanitizePositionLimit(kFloatMax, 0.30f) == 0.5f, "float max clamps");
}

void FovScaleTests() {
    std::printf("SanitizeFovScale\n");
    Check(SanitizeFovScale(1.0f) == 1.0f, "1 passes through, and writes nothing downstream");
    Check(SanitizeFovScale(1.6f) == 1.6f, "an in-range widening passes through");
    Check(SanitizeFovScale(kMinFovScale) == kMinFovScale, "the narrow bound passes through");
    Check(SanitizeFovScale(kMaxFovScale) == kMaxFovScale, "the wide bound passes through");

    // A mistyped 100 - reaching for degrees rather than a multiplier - must land
    // on a wide view rather than on a frustum nothing can be drawn in.
    Check(SanitizeFovScale(100.0f) == kMaxFovScale, "above the range clamps to the wide bound");
    Check(SanitizeFovScale(0.1f) == kMinFovScale, "below it clamps to the narrow bound");

    // Zero would divide the projection terms by nothing; a negative would mirror
    // the frame. Both land on the narrow bound rather than reaching the matrix.
    Check(SanitizeFovScale(0.0f) == kMinFovScale, "zero clamps rather than dividing by it");
    Check(SanitizeFovScale(-1.5f) == kMinFovScale, "and a negative cannot mirror the frame");

    Check(SanitizeFovScale(kNan) == 1.0f, "a NaN falls back to 1, which writes nothing");
    Check(SanitizeFovScale(kInf) == 1.0f, "and so does an infinity");
    Check(SanitizeFovScale(kFloatMax) == kMaxFovScale, "the top of the float range clamps");
}

void VirtualKeyTests() {
    std::printf("IsBindableVirtualKey\n");
    Check(IsBindableVirtualKey(0x22), "Page Down can be bound");
    Check(IsBindableVirtualKey(0x07), "low bound can be bound");
    Check(IsBindableVirtualKey(0xFE), "high bound can be bound");
    Check(IsBindableVirtualKey(0x54), "a chord letter can be bound");
    Check(IsBindableVirtualKey(0x7B), "F12 can be bound");

    // A key the poller can never see makes the hotkey silently do nothing.
    Check(!IsBindableVirtualKey(0x220), "a code above 0xFE is not a key");
    Check(!IsBindableVirtualKey(0), "0 is not a key");
    Check(!IsBindableVirtualKey(-1), "a negative code is not a key");

    // GetAsyncKeyState reports the mouse buttons exactly like keys, so binding
    // one works - and `ToggleKey=1`, a plausible reach for the "1" key (0x31),
    // would toggle head tracking on every left click. 0x02 is right mouse,
    // which is camera look.
    Check(!IsBindableVirtualKey(0x01), "left mouse cannot be bound");
    Check(!IsBindableVirtualKey(0x02), "right mouse cannot be bound");
    Check(!IsBindableVirtualKey(0x04), "middle mouse cannot be bound");
    Check(!IsBindableVirtualKey(0x06), "the last mouse code cannot be bound");

    // Bound to a modifier, a nav binding is suppressed by the chord guard and a
    // chord binding fires the moment the chord itself is held.
    Check(!IsBindableVirtualKey(0x10), "Shift cannot be bound");
    Check(!IsBindableVirtualKey(0x11), "Control cannot be bound");
    Check(!IsBindableVirtualKey(0x12), "Alt cannot be bound");
    Check(!IsBindableVirtualKey(0xA2), "left Control cannot be bound");
    Check(!IsBindableVirtualKey(0xA5), "right Alt cannot be bound");
}

void UdpPortTests() {
    std::printf("NormalizeUdpPort (the INI port boundary this mod relies on)\n");
    bool valid = false;

    Check(cameraunlock::NormalizeUdpPort(4242, 4242, valid) == 4242 && valid,
          "default port is valid");
    Check(cameraunlock::NormalizeUdpPort(1024, 4242, valid) == 1024 && valid,
          "low bound is valid");
    Check(cameraunlock::NormalizeUdpPort(65535, 4242, valid) == 65535 && valid,
          "high bound is valid");

    // 70000 & 0xFFFF == 4464: a raw cast to uint16_t would bind a wrong port
    // and the mod would sit there reporting it was listening.
    Check(cameraunlock::NormalizeUdpPort(70000, 4242, valid) == 4242 && !valid,
          "above 65535 -> fallback, not truncated to 4464");
    Check(cameraunlock::NormalizeUdpPort(0, 4242, valid) == 4242 && !valid,
          "0 (ephemeral bind) -> fallback");
    Check(cameraunlock::NormalizeUdpPort(-1, 4242, valid) == 4242 && !valid,
          "negative -> fallback");
    Check(cameraunlock::NormalizeUdpPort(80, 4242, valid) == 4242 && !valid,
          "privileged port -> fallback");
}

}  // namespace

int main() {
    std::printf("SnowRunner head tracking - config boundary tests\n");
    std::printf("======================================================\n");
    SmoothingTests();
    SensitivityTests();
    PositionLimitTests();
    FovScaleTests();
    VirtualKeyTests();
    UdpPortTests();
    return sr_test::Summary("config boundary");
}
