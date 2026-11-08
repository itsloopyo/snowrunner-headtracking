// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "vehicle_camera_gate.h"

#include "test_support.h"

using sr_ht::VehicleCameraGate;
using sr_test::Check;

namespace {

void ShutBeforeTheFirstDriveUpdate() {
    VehicleCameraGate gate;
    gate.OnFrame();
    Check(!gate.Active(), "no drive update yet: not a driving frame");
    gate.OnFrame();
    Check(!gate.Active(), "still none: still not a driving frame");
}

void OpenWhileTheDriveCameraUpdatesEveryFrame() {
    VehicleCameraGate gate;
    bool all = true;
    for (int i = 0; i < 100; ++i) {
        gate.OnDriveUpdate();
        gate.OnFrame();
        all = all && gate.Active();
    }
    Check(all, "an update every frame keeps every frame a driving frame");
}

// A frame that took 800ms is still one frame: the gate counts frames, so how
// long a frame took cannot shut it.
void UnevenUpdatesDoNotShutIt() {
    VehicleCameraGate gate;
    gate.OnDriveUpdate();
    gate.OnFrame();
    bool all = gate.Active();
    for (int i = 0; i < 50; ++i) {
        gate.OnFrame();  // the update landed after this frame's visibility pass
        all = all && gate.Active();
        gate.OnDriveUpdate();
        gate.OnDriveUpdate();
        gate.OnFrame();
        all = all && gate.Active();
    }
    Check(all, "alternating zero and two updates per frame never shuts the gate");
}

void ShutsOnTheSecondQuietFrame() {
    VehicleCameraGate gate;
    gate.OnDriveUpdate();
    gate.OnFrame();
    gate.OnFrame();
    Check(gate.Active(), "one quiet frame is still a driving frame");
    gate.OnFrame();
    Check(!gate.Active(), "two quiet frames in a row is a menu");
    for (int i = 0; i < 1000; ++i) gate.OnFrame();
    Check(!gate.Active(), "a long menu stays shut");
}

void OpensOnTheFrameDrivingResumes() {
    VehicleCameraGate gate;
    gate.OnDriveUpdate();
    gate.OnFrame();
    for (int i = 0; i < 10; ++i) gate.OnFrame();
    gate.OnDriveUpdate();
    gate.OnFrame();
    Check(gate.Active(), "the first frame with an update after a menu is a driving frame");
}

}  // namespace

int main() {
    ShutBeforeTheFirstDriveUpdate();
    OpenWhileTheDriveCameraUpdatesEveryFrame();
    UnevenUpdatesDoNotShutIt();
    ShutsOnTheSecondQuietFrame();
    OpensOnTheFrameDrivingResumes();
    return sr_test::Summary("vehicle camera gate");
}
