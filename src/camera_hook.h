// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace sr_ht {

// Uses one head pose for player visibility and camera upload. Vehicle-camera
// activity gates tracking out of menus. Installation failure removes all hooks
// created here.
bool InstallCameraHook();

}  // namespace sr_ht
