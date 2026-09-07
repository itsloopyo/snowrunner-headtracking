// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace sr_ht {

constexpr int kCameraMatrixFloats = 16;

struct HeadPose {
    float yaw = 0.0f;
    float pitch = 0.0f;
    float roll = 0.0f;
    float lean_x = 0.0f;
    float lean_y = 0.0f;
    float lean_z = 0.0f;
};

// The renderer consumes a row-major view matrix whose first three columns are
// the camera's world-space right, up and forward axes. Rotating those axes and
// rebuilding the translation from the unchanged eye turns the view in place.
// Positional tracking moves the eye along the clean camera axes, then rebuilds
// both matrices so every render constant describes the same camera.
//
// `world_yaw` selects the axis head yaw turns about; pitch and roll are always
// camera-local. World up is the default, because the chase camera looks down at
// the truck and a camera-local yaw in that attitude sweeps the view around a
// cone rather than turning it - measured on a camera pitched down 22 degrees, it
// tips the right axis 0.24 out of level and turns the heading 42.1 degrees for a
// 40 degree head movement. Camera-local is the other position because the cabin
// camera banks with the truck, and a truck on its roof has a world up that means
// nothing to the driver.
void ApplyHeadPoseToRenderCamera(float view[kCameraMatrixFloats], float eye[3],
                                 const float projection[kCameraMatrixFloats],
                                 float view_projection[kCameraMatrixFloats],
                                 const HeadPose& pose, bool world_yaw);

}  // namespace sr_ht
