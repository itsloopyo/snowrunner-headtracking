// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "vehicle_camera_gate.h"

#include "test_support.h"

#include <cstdint>

using sr_ht::VehicleCameraGate;
using sr_test::Check;

namespace {

void ShutBeforeTheFirstDriveUpdate() {
    VehicleCameraGate gate;
    gate.OnFrame(0);
    Check(!gate.Active(), "no drive update yet: not a driving frame");
    gate.OnFrame(16);
    Check(!gate.Active(), "still none: still not a driving frame");
}

void OpenWhileTheDriveCameraUpdatesEveryFrame() {
    VehicleCameraGate gate;
    bool all = true;
    for (int i = 0; i < 100; ++i) {
        gate.OnDriveUpdate();
        gate.OnFrame(1000 + i * 16);
        all = all && gate.Active();
    }
    Check(all, "an update every frame keeps every frame a driving frame");
}

// A frame that took 800ms is still one frame: how long a frame took cannot shut
// the gate while updates keep arriving frame to frame.
void UnevenUpdatesOnSlowFramesDoNotShutIt() {
    VehicleCameraGate gate;
    std::uint64_t now = 1000;
    gate.OnDriveUpdate();
    gate.OnFrame(now);
    bool all = gate.Active();
    for (int i = 0; i < 50; ++i) {
        now += 800;
        gate.OnFrame(now);  // the update landed after this frame's visibility pass
        all = all && gate.Active();
        gate.OnDriveUpdate();
        gate.OnDriveUpdate();
        now += 800;
        gate.OnFrame(now);
        all = all && gate.Active();
    }
    Check(all, "alternating zero and two updates per 800ms frame never shuts the gate");
}

// The drive camera ticks at 60 Hz whatever the frame rate, so at 400 fps a tick
// is followed by several frames with none.
void SeveralFramesPerTickDoNotShutIt() {
    VehicleCameraGate gate;
    bool all = true;
    for (int tick = 0; tick < 60; ++tick) {
        gate.OnDriveUpdate();
        for (int frame = 0; frame < 7; ++frame) {
            gate.OnFrame(1000 + tick * 17 + frame * 2);
            all = all && gate.Active();
        }
    }
    Check(all, "seven frames to every 60 Hz tick keep every frame a driving frame");
}

// Open on every quiet frame up to kQuietMs after the update, shut on the first
// one past it, whatever the frame rate.
void OpenForExactlyTheTimeAllowance(std::uint64_t frame_ms, const char* what) {
    VehicleCameraGate gate;
    gate.OnDriveUpdate();
    gate.OnFrame(1000);
    bool open = gate.Active();
    std::uint64_t now = 1000;
    while (now + frame_ms - 1000 <= VehicleCameraGate::kQuietMs) {
        gate.OnFrame(now += frame_ms);
        open = open && gate.Active();
    }
    Check(open, what);
    gate.OnFrame(now += frame_ms);
    Check(!gate.Active(), "the first quiet frame past kQuietMs is shut: the drive camera has stopped");
    for (int i = 0; i < 1000; ++i) gate.OnFrame(now += frame_ms);
    Check(!gate.Active(), "a long menu stays shut");
}

void OpenForExactlyTheTimeAllowanceAtEveryRate() {
    OpenForExactlyTheTimeAllowance(1, "at 1000 fps every quiet frame within kQuietMs is open");
    OpenForExactlyTheTimeAllowance(4, "at 250 fps every quiet frame within kQuietMs is open");
    OpenForExactlyTheTimeAllowance(16, "at 60 fps every quiet frame within kQuietMs is open");
    OpenForExactlyTheTimeAllowance(25, "a frame landing exactly on kQuietMs is still open");
}

void ShutsOnTheSecondSlowQuietFrame() {
    VehicleCameraGate gate;
    gate.OnDriveUpdate();
    gate.OnFrame(1000);
    gate.OnFrame(1300);
    Check(gate.Active(), "one slow quiet frame is still a driving frame");
    gate.OnFrame(1600);
    Check(!gate.Active(), "two slow quiet frames in a row is a menu");
}

// A hitch longer than kQuietMs with no tick in it, then a fast frame with none
// either: both allowances are spent, so that frame is drawn untracked. It needs
// the 60 Hz drive tick to produce nothing across the hitch, which no run showed.
void AHitchThenAFastQuietFrameShuts() {
    VehicleCameraGate gate;
    gate.OnDriveUpdate();
    gate.OnFrame(1000);
    gate.OnFrame(1000 + VehicleCameraGate::kQuietMs + 50);
    Check(gate.Active(), "the hitch frame itself is the one quiet frame allowed");
    gate.OnFrame(1000 + VehicleCameraGate::kQuietMs + 55);
    Check(!gate.Active(), "a second quiet frame past kQuietMs is shut");
}

void OpensOnTheFrameDrivingResumes() {
    VehicleCameraGate gate;
    gate.OnDriveUpdate();
    std::uint64_t now = 1000;
    gate.OnFrame(now);
    for (int i = 0; i < 20; ++i) gate.OnFrame(now += 16);
    gate.OnDriveUpdate();
    gate.OnFrame(now += 16);
    Check(gate.Active(), "the first frame with an update after a menu is a driving frame");
}

}  // namespace

int main() {
    ShutBeforeTheFirstDriveUpdate();
    OpenWhileTheDriveCameraUpdatesEveryFrame();
    UnevenUpdatesOnSlowFramesDoNotShutIt();
    SeveralFramesPerTickDoNotShutIt();
    OpenForExactlyTheTimeAllowanceAtEveryRate();
    ShutsOnTheSecondSlowQuietFrame();
    AHitchThenAFastQuietFrameShuts();
    OpensOnTheFrameDrivingResumes();
    return sr_test::Summary("vehicle camera gate");
}
