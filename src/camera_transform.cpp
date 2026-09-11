// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_transform.h"

#include <cmath>

#include "camera_fov.h"

#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/vec3.h"

namespace sr_ht {

namespace {

using cameraunlock::math::Quat4;
using cameraunlock::math::Vec3;

constexpr float kDegreesToRadians = 0.01745329252f;

Vec3 Column(const float matrix[kCameraMatrixFloats], int column) {
    return Vec3(matrix[column], matrix[4 + column], matrix[8 + column]);
}

Vec3 FromCameraBasis(const float view[kCameraMatrixFloats], const Vec3& local) {
    return Column(view, 0) * local.x
         + Column(view, 1) * local.y
         + Column(view, 2) * local.z;
}

void SetColumn(float matrix[kCameraMatrixFloats], int column, const Vec3& value) {
    matrix[column] = value.x;
    matrix[4 + column] = value.y;
    matrix[8 + column] = value.z;
}

// World up. Read off the running game: the drive camera's rig reported its up
// row as (0.000, 1.000, 0.000) with the truck parked, and its eye sits above the
// point it looks at on the same axis.
Vec3 RotateAboutWorldUp(const Vec3& v, float radians) {
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return Vec3(v.x * c + v.z * s, v.y, v.z * c - v.x * s);
}

void Multiply(const float left[kCameraMatrixFloats],
              const float right[kCameraMatrixFloats],
              float out[kCameraMatrixFloats]) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            float value = 0.0f;
            for (int k = 0; k < 4; ++k) {
                value += left[row * 4 + k] * right[k * 4 + column];
            }
            out[row * 4 + column] = value;
        }
    }
}

// The pose half of the composition, as measured in the running game: rotate the
// camera basis, move the eye along the CLEAN axes, and rebuild the translation
// so the view turns in place rather than orbiting.
void ApplyHeadPose(float view[kCameraMatrixFloats], float eye[3],
                   const HeadPose& pose, bool world_yaw) {
    float clean[kCameraMatrixFloats];
    for (int i = 0; i < kCameraMatrixFloats; ++i) clean[i] = view[i];

    // Yaw and roll agree with this view matrix; only pitch runs opposite,
    // OpenTrack's positive pitch looking up being a negative right-axis rotation
    // in the renderer's right-handed camera frame.
    //
    // Yaw and roll were negated here until the first build that reached the
    // screen was played, and both came back mirrored. The protocol carries no
    // statement of what its positive directions mean, so this is the only way
    // the signs can be settled.
    //
    // Head yaw turns about WORLD up, not the camera's own up axis. The chase
    // camera looks down at the truck, and yawing about its own up in that
    // attitude sweeps the view around a cone: the horizon tips, and looking
    // steeply down turns the world about the middle of the screen instead of
    // turning the head. Pitch and roll stay camera-local, so the pitch axis is
    // the camera's right after the yaw has been applied - which is what a person
    // turning and then looking up actually does.
    // In world-yaw mode the yaw is held out of the camera-local quaternion and
    // applied about world up afterwards, so the pitch axis is the camera's right
    // AFTER the yaw - a person turning and then looking up.
    const Quat4 rotation = world_yaw
        ? Quat4::FromYawPitchRoll(0.0f, -pose.pitch, pose.roll)
        : Quat4::FromYawPitchRoll(pose.yaw, -pose.pitch, pose.roll);
    const float yaw = world_yaw ? pose.yaw * kDegreesToRadians : 0.0f;
    const Vec3 right =
        RotateAboutWorldUp(FromCameraBasis(clean, rotation.Rotate(Vec3::Right())), yaw);
    const Vec3 up =
        RotateAboutWorldUp(FromCameraBasis(clean, rotation.Rotate(Vec3::Up())), yaw);
    const Vec3 forward =
        RotateAboutWorldUp(FromCameraBasis(clean, rotation.Rotate(Vec3::Forward())), yaw);
    SetColumn(view, 0, right);
    SetColumn(view, 1, up);
    SetColumn(view, 2, forward);

    const Vec3 clean_right = Column(clean, 0);
    const Vec3 clean_up = Column(clean, 1);
    const Vec3 clean_forward = Column(clean, 2);
    // The lateral lean is mirrored the same way yaw and roll were, and was
    // reported in game alongside them. Its symptom is subtler than theirs -
    // leaning looks like it works, it just goes the wrong way.
    Vec3 moved_eye(eye[0], eye[1], eye[2]);
    moved_eye = moved_eye - clean_right * pose.lean_x
                         + clean_up * pose.lean_y
                         - clean_forward * pose.lean_z;
    eye[0] = moved_eye.x;
    eye[1] = moved_eye.y;
    eye[2] = moved_eye.z;

    view[12] = -Vec3::Dot(moved_eye, right);
    view[13] = -Vec3::Dot(moved_eye, up);
    view[14] = -Vec3::Dot(moved_eye, forward);
}

}  // namespace

void ApplyHeadPoseToRenderCamera(float view[kCameraMatrixFloats], float eye[3],
                                 float projection[kCameraMatrixFloats],
                                 float view_projection[kCameraMatrixFloats],
                                 float& vertical_fov_radians, const HeadPose& pose,
                                 bool world_yaw, float fov_degrees) {
    const bool moves_the_camera =
        pose.yaw != 0.0f || pose.pitch != 0.0f || pose.roll != 0.0f
        || pose.lean_x != 0.0f || pose.lean_y != 0.0f || pose.lean_z != 0.0f;
    // Computed from THIS frame's projection, not once at load: the cabin and
    // the chase view are built from separate Field of View settings, so the
    // scale that lands on the configured angle differs between them and moves
    // whenever the player changes either setting.
    const float fov_scale = FovScaleForTarget(projection, fov_degrees);
    const bool widens_the_frustum = fov_scale != 1.0f;
    if (!moves_the_camera && !widens_the_frustum) return;

    if (moves_the_camera) ApplyHeadPose(view, eye, pose, world_yaw);
    if (widens_the_frustum) {
        ScaleProjectionFieldOfView(projection, fov_scale);
        // Frustum rebuilds and view rays read the angle, not the cached matrix.
        vertical_fov_radians = 2.0f * std::atan(1.0f / projection[kProjectionVertical]);
    }

    // Last, and from whatever the two above left behind, so every matrix in the
    // record describes one camera.
    Multiply(view, projection, view_projection);
}

void ExpandCullingFrustum(const float view[kCameraMatrixFloats],
                         float projection[kCameraMatrixFloats],
                         float view_projection[kCameraMatrixFloats], float& vertical_fov_radians) {
    // Keep visibility planes outside the widened view, at every configured angle.
    ScaleProjectionFieldOfView(projection, 1.05f);
    vertical_fov_radians = 2.0f * std::atan(1.0f / projection[kProjectionVertical]);
    Multiply(view, projection, view_projection);
}

}  // namespace sr_ht
