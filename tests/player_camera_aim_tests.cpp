// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "player_camera_aim.h"

#include "test_support.h"

#include <cstdint>
#include <cstring>
#include <string>

using sr_ht::CheckLookAt;
using sr_ht::PlayerCameraAimGate;
using sr_test::Check;

namespace {

void ShutBeforeTheFirstAim() {
    PlayerCameraAimGate gate;
    gate.OnFrame();
    Check(!gate.Active(), "no LookAt yet: not the vehicle camera's frame");
}

void OpenWhileEveryFrameIsAimed() {
    PlayerCameraAimGate gate;
    bool all = true;
    for (int i = 0; i < 100; ++i) {
        gate.OnAim();
        gate.OnFrame();
        all = all && gate.Active();
    }
    Check(all, "one LookAt before every frame keeps every frame the vehicle camera's");
}

// The map draws through the player's record every frame and never aims it.
void ShutOnTheFirstMapFrameAndAfter() {
    PlayerCameraAimGate gate;
    gate.OnAim();
    gate.OnFrame();
    gate.OnFrame();
    Check(!gate.Active(), "the first frame with no LookAt is not the vehicle camera's");
    bool none = true;
    for (int i = 0; i < 300; ++i) {
        gate.OnFrame();
        none = none && !gate.Active();
    }
    Check(none, "a map held open stays shut on every frame");
}

// A load's own LookAt can land in the same frame as the vehicle camera's.
void TwoAimsInOneFrameStillOneFrame() {
    PlayerCameraAimGate gate;
    gate.OnAim();
    gate.OnAim();
    gate.OnFrame();
    Check(gate.Active(), "two LookAts before a frame open it");
    gate.OnFrame();
    Check(!gate.Active(), "and the next frame with none is shut, not carried by the extra one");
}

void OpenOnTheFrameTheMapCloses() {
    PlayerCameraAimGate gate;
    for (int i = 0; i < 10; ++i) gate.OnFrame();
    gate.OnAim();
    gate.OnFrame();
    Check(gate.Active(), "the first aimed frame after the map is the vehicle camera's");
}

const std::uint8_t kLookAtHead[] = {
    0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18,
    0x57, 0x48, 0x83, 0xEC, 0x40, 0xF3, 0x41, 0x0F, 0x10, 0x00, 0x48, 0x8B, 0xEA, 0xF3, 0x0F,
    0x5C, 0x02, 0xF3, 0x41, 0x0F, 0x10, 0x48, 0x04, 0x48, 0x8B, 0xF9, 0xF3, 0x0F, 0x5C};
static_assert(sizeof(kLookAtHead) == sr_ht::kLookAtCheckedBytes, "the test covers every byte the check reads");

void AcceptsTheShippedLookAt() {
    std::string error;
    Check(CheckLookAt(kLookAtHead, error), "the LookAt every profiled build ships is accepted");
}

void RefusesAnythingElse() {
    for (std::size_t i = 0; i < sizeof(kLookAtHead); ++i) {
        std::uint8_t code[sizeof(kLookAtHead)];
        std::memcpy(code, kLookAtHead, sizeof(code));
        code[i] ^= 0x01;
        std::string error;
        if (CheckLookAt(code, error) || error.empty()) {
            Check(false, "a LookAt that differs in any checked byte is refused with a reason");
            return;
        }
    }
    Check(true, "a LookAt that differs in any checked byte is refused with a reason");
}

}  // namespace

int main() {
    ShutBeforeTheFirstAim();
    OpenWhileEveryFrameIsAimed();
    ShutOnTheFirstMapFrameAndAfter();
    TwoAimsInOneFrameStillOneFrame();
    OpenOnTheFrameTheMapCloses();
    AcceptsTheShippedLookAt();
    RefusesAnythingElse();
    return sr_test::Summary("player camera aim");
}
