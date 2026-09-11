// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace sr_ht {

// Uses one head pose for player visibility and camera upload. Vehicle-camera
// activity gates tracking out of menus. Installation failure removes all hooks
// created here.
//
// `fov_degrees` is the INI's Fov - the angle the frame should span across its
// width - already held at 0 or inside kMinFov..kMaxFov by SanitizeFov. It is
// taken here rather than through a setter because every hook this installs
// reads it, and passing it in makes the one write happen before any of them can
// run.
bool InstallCameraHook(float fov_degrees);

}  // namespace sr_ht
