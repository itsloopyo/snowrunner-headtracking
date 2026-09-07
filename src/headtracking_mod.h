// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "camera_transform.h"

namespace sr_ht {

// Pins this module and hands the bootstrap to a new thread. There is no
// matching teardown: the pin makes the module permanently resident, so the
// detour, the receiver and the hotkey thread all live until the process exits,
// and the process exiting reclaims them. Tearing them down from DllMain is what
// a teardown would have to do, and joining a thread under the loader lock
// deadlocks.
void Initialize();

// The pose to use for this frame, and whether to use it at all. Advances the
// pipeline - the frame clock, the interpolator and the smoothing all step here -
// so there is exactly one caller, the camera hook, and it calls this once per
// frame. Returns false when no tracker data has arrived or the gameplay gate is
// shut, in which case the engine's camera is left exactly as it computed it.
bool PoseForThisFrame(HeadPose& pose);

// Whether head yaw turns about world up rather than the camera's own up axis.
// Toggled by the player, world by default; read once per composed frame.
bool WorldYawEnabled();

}  // namespace sr_ht
