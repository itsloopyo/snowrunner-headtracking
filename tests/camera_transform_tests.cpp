// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_fov.h"
#include "camera_transform.h"

#include "test_support.h"

#include <cmath>
#include <cstdio>
#include <cstring>

using namespace sr_ht;
using sr_test::Check;
using sr_test::CheckClose;

namespace {

constexpr float kDegreesToRadians = 0.01745329252f;
constexpr float kProjection[kCameraMatrixFloats] = {
    1.9022f, 0.0f, 0.0f, 0.0f,
    0.0f, 3.3817f, 0.0f, 0.0f,
    0.0f, 0.0f, -0.0001f, 1.0f,
    0.0f, 0.0f, 0.5001f, 0.0f,
};

struct Camera {
    float view[kCameraMatrixFloats];
    float eye[3];
    float projection[kCameraMatrixFloats];
    float view_projection[kCameraMatrixFloats];
};

float Dot(const float* a, const float* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

void Column(const float* matrix, int column, float out[3]) {
    out[0] = matrix[column];
    out[1] = matrix[4 + column];
    out[2] = matrix[8 + column];
}

void Multiply(const float* left, const float* right, float* out) {
    for (int row = 0; row < 4; ++row) {
        for (int column = 0; column < 4; ++column) {
            out[row * 4 + column] = 0.0f;
            for (int k = 0; k < 4; ++k) {
                out[row * 4 + column] +=
                    left[row * 4 + k] * right[k * 4 + column];
            }
        }
    }
}

Camera MakeCamera(const float right[3], const float up[3], const float forward[3],
                  const float eye[3]) {
    Camera camera{};
    for (int row = 0; row < 3; ++row) {
        camera.view[row * 4] = right[row];
        camera.view[row * 4 + 1] = up[row];
        camera.view[row * 4 + 2] = forward[row];
    }
    camera.view[12] = -Dot(eye, right);
    camera.view[13] = -Dot(eye, up);
    camera.view[14] = -Dot(eye, forward);
    camera.view[15] = 1.0f;
    std::memcpy(camera.eye, eye, sizeof(camera.eye));
    std::memcpy(camera.projection, kProjection, sizeof(camera.projection));
    Multiply(camera.view, camera.projection, camera.view_projection);
    return camera;
}

Camera LevelCamera() {
    const float right[3] = {1.0f, 0.0f, 0.0f};
    const float up[3] = {0.0f, 1.0f, 0.0f};
    const float forward[3] = {0.0f, 0.0f, 1.0f};
    const float eye[3] = {10.0f, 20.0f, 30.0f};
    return MakeCamera(right, up, forward, eye);
}

// The chase camera as the game actually presents it: looking down at the truck
// by 22 degrees. This is the attitude that tells world yaw apart from camera
// local yaw - on a level camera the two are identical, which is why the level
// fixture cannot protect the distinction.
Camera PitchedCamera() {
    const float d = 22.0f * kDegreesToRadians;
    const float right[3] = {1.0f, 0.0f, 0.0f};
    const float up[3] = {0.0f, std::cos(d), std::sin(d)};
    const float forward[3] = {0.0f, -std::sin(d), std::cos(d)};
    const float eye[3] = {-805.283f, 43.578f, -783.155f};
    return MakeCamera(right, up, forward, eye);
}

Camera BankedCamera() {
    const float right[3] = {0.0f, 1.0f, 0.0f};
    const float up[3] = {-1.0f, 0.0f, 0.0f};
    const float forward[3] = {0.0f, 0.0f, 1.0f};
    const float eye[3] = {-805.283f, 43.578f, -783.155f};
    return MakeCamera(right, up, forward, eye);
}

// World yaw is the shipped default, so it is what the unqualified helper uses
// and what every other case here is written against.
void Apply(Camera& camera, const HeadPose& pose, bool world_yaw = true,
           float fov_scale = 1.0f) {
    ApplyHeadPoseToRenderCamera(camera.view, camera.eye, camera.projection,
                                camera.view_projection, pose, world_yaw, fov_scale);
}

void CheckVector(const float actual[3], const float expected[3], const char* what) {
    char component[160];
    for (int i = 0; i < 3; ++i) {
        std::snprintf(component, sizeof(component), "%s component %d", what, i);
        CheckClose(actual[i], expected[i], component);
    }
}

void RotationIsOneToOneAndDoesNotOrbit() {
    std::printf("\nrotation is 1:1 and leaves the eye fixed\n");
    const float c30 = std::cos(30.0f * kDegreesToRadians);
    const float s30 = std::sin(30.0f * kDegreesToRadians);

    Camera yaw = LevelCamera();
    const Camera yaw_clean = yaw;
    HeadPose pose;
    pose.yaw = 30.0f;
    Apply(yaw, pose);
    const float expected_forward[3] = {s30, 0.0f, c30};
    float actual[3];
    Column(yaw.view, 2, actual);
    CheckVector(actual, expected_forward, "yaw +30 turns the view right by 30 degrees");
    Check(std::memcmp(yaw.eye, yaw_clean.eye, sizeof(yaw.eye)) == 0,
          "yaw does not move the eye");

    Camera pitch = LevelCamera();
    const Camera pitch_clean = pitch;
    pose = HeadPose();
    pose.pitch = 30.0f;
    Apply(pitch, pose);
    const float expected_pitch[3] = {0.0f, s30, c30};
    Column(pitch.view, 2, actual);
    CheckVector(actual, expected_pitch, "pitch +30 looks up by 30 degrees");
    Check(std::memcmp(pitch.eye, pitch_clean.eye, sizeof(pitch.eye)) == 0,
          "pitch does not move the eye");

    Camera roll = LevelCamera();
    const Camera roll_clean = roll;
    pose = HeadPose();
    pose.roll = 30.0f;
    Apply(roll, pose);
    const float expected_right[3] = {c30, s30, 0.0f};
    Column(roll.view, 0, actual);
    CheckVector(actual, expected_right, "roll +30 tilts the view by 30 degrees");
    Check(std::memcmp(roll.eye, roll_clean.eye, sizeof(roll.eye)) == 0,
          "roll does not move the eye");
}

void RotationUsesTheCameraLocalAxes() {
    std::printf("\nrotation follows the camera local axes\n");
    Camera camera = BankedCamera();
    HeadPose pose;
    pose.pitch = 25.0f;
    Apply(camera, pose);

    float forward[3];
    Column(camera.view, 2, forward);
    const float expected[3] = {
        -std::sin(25.0f * kDegreesToRadians),
        0.0f,
        std::cos(25.0f * kDegreesToRadians),
    };
    CheckVector(forward, expected, "pitch follows the banked camera's right axis");
}

void YawTurnsAboutWorldUp() {
    std::printf("\nyaw turns about world up, not the camera's own up axis\n");

    // A rotation about world up cannot change any vector's height component.
    // That is the whole definition of it, it is independent of how this is
    // implemented, and it is exactly what camera local yaw fails: yawing a
    // camera that is pitched down tips its axes out of level and the horizon
    // rolls with them.
    struct Case { const char* what; Camera camera; };
    Case cases[] = {
        { "pitched down at the truck", PitchedCamera() },
        { "banked onto its side", BankedCamera() },
    };

    char label[160];
    for (Case& c : cases) {
        const Camera clean = c.camera;
        HeadPose pose;
        pose.yaw = 40.0f;
        Apply(c.camera, pose);

        for (int axis = 0; axis < 3; ++axis) {
            float before[3], after[3];
            Column(clean.view, axis, before);
            Column(c.camera.view, axis, after);
            std::snprintf(label, sizeof(label),
                          "yaw leaves axis %d level on a camera %s", axis, c.what);
            CheckClose(after[1], before[1], label);

            std::snprintf(label, sizeof(label),
                          "yaw keeps axis %d a unit vector on a camera %s", axis, c.what);
            CheckClose(std::sqrt(Dot(after, after)), 1.0f, label);
        }

        std::snprintf(label, sizeof(label), "yaw does not move the eye on a camera %s", c.what);
        Check(std::memcmp(c.camera.eye, clean.eye, sizeof(clean.eye)) == 0, label);
    }

    // And it still turns by the angle asked: on the pitched camera the heading
    // of the view, taken in the ground plane, moves by exactly the head angle.
    Camera camera = PitchedCamera();
    const Camera clean = camera;
    HeadPose pose;
    pose.yaw = 40.0f;
    Apply(camera, pose);
    float before[3], after[3];
    Column(clean.view, 2, before);
    Column(camera.view, 2, after);
    const float turned =
        (std::atan2(after[0], after[2]) - std::atan2(before[0], before[2])) / kDegreesToRadians;
    CheckClose(turned, 40.0f, "the view heading turns by the angle the tracker sent");
}

void CameraLocalYawIsTheOtherPosition() {
    std::printf("\nthe yaw mode toggle actually changes the axis\n");

    // The two modes agree exactly on a level camera - its own up IS world up -
    // so a test that only ever used a level camera could not tell them apart,
    // and the suite did not until this was added.
    Camera level_world = LevelCamera();
    Camera level_local = LevelCamera();
    HeadPose pose;
    pose.yaw = 35.0f;
    Apply(level_world, pose, true);
    Apply(level_local, pose, false);
    // Compared with a tolerance, not byte for byte: the two modes reach the same
    // matrix by different arithmetic - one zeroes the quaternion's yaw and turns
    // about world up afterwards, the other does neither - so they land a few
    // ulps apart.
    for (int i = 0; i < kCameraMatrixFloats; ++i) {
        CheckClose(level_world.view[i], level_local.view[i],
                   "on a level camera the two modes agree");
    }

    // On a pitched camera they must not agree: camera-local yaw tips the axes
    // out of level, which is the cone sweep world yaw exists to avoid.
    Camera pitched_world = PitchedCamera();
    Camera pitched_local = PitchedCamera();
    Apply(pitched_world, pose, true);
    Apply(pitched_local, pose, false);
    Check(std::memcmp(pitched_world.view, pitched_local.view, sizeof(pitched_world.view)) != 0,
          "on a pitched camera the two modes differ");

    float world_right[3], local_right[3];
    Column(pitched_world.view, 0, world_right);
    Column(pitched_local.view, 0, local_right);
    Check(std::fabs(world_right[1]) < 1e-5f, "world yaw keeps the right axis level");
    Check(std::fabs(local_right[1]) > 0.1f, "camera local yaw tips it out of level");
}

void LeanMovesAlongTheCleanCameraAxes() {
    std::printf("\nlean moves the eye without rotating the camera\n");
    Camera camera = BankedCamera();
    const Camera clean = camera;
    HeadPose pose;
    pose.lean_x = 0.30f;
    pose.lean_y = 0.20f;
    pose.lean_z = -0.40f;
    Apply(camera, pose);

    const float expected_eye[3] = {
        clean.eye[0] - 0.20f,
        clean.eye[1] - 0.30f,
        clean.eye[2] + 0.40f,
    };
    CheckVector(camera.eye, expected_eye, "lean lands on the clean camera axes");
    Check(std::memcmp(camera.view, clean.view, sizeof(float) * 12) == 0,
          "lean does not rotate the view basis");
}

void ViewTranslationAndCachedProductFollowThePose() {
    std::printf("\nview translation and cached product describe the same camera\n");
    Camera camera = BankedCamera();
    HeadPose pose;
    pose.yaw = 37.0f;
    pose.pitch = -18.0f;
    pose.roll = 11.0f;
    pose.lean_x = -0.25f;
    pose.lean_y = 0.12f;
    pose.lean_z = -0.33f;
    Apply(camera, pose);

    for (int column = 0; column < 3; ++column) {
        float axis[3];
        Column(camera.view, column, axis);
        CheckClose(camera.view[12 + column], -Dot(camera.eye, axis),
                   "view translation is negative eye dot axis");
    }

    float expected[kCameraMatrixFloats];
    Multiply(camera.view, camera.projection, expected);
    for (int i = 0; i < kCameraMatrixFloats; ++i) {
        CheckClose(camera.view_projection[i], expected[i],
                   "cached view-projection is rebuilt");
    }
}

void ZeroPoseIsByteExact() {
    std::printf("\na zero pose changes no renderer data\n");
    Camera camera = BankedCamera();
    const Camera clean = camera;
    Apply(camera, HeadPose());
    Check(std::memcmp(&camera, &clean, sizeof(camera)) == 0,
          "the complete camera record is unchanged");
}

// FovScale is a rendering setting, not a pose, so the two have to compose
// without either touching the other's data.
void FieldOfViewScalesTheFrustumOnly() {
    std::printf("\nFovScale widens the frustum and nothing else\n");
    Camera camera = PitchedCamera();
    const Camera clean = camera;
    Apply(camera, HeadPose(), true, 1.25f);

    CheckClose(camera.projection[kProjectionHorizontal],
               clean.projection[kProjectionHorizontal] / 1.25f,
               "the horizontal term is divided by the scale");
    CheckClose(camera.projection[kProjectionVertical],
               clean.projection[kProjectionVertical] / 1.25f,
               "and so is the vertical one");
    for (int i = 0; i < kCameraMatrixFloats; ++i) {
        if (i == kProjectionHorizontal || i == kProjectionVertical) continue;
        CheckClose(camera.projection[i], clean.projection[i],
                   "every other projection element is untouched");
    }
    Check(std::memcmp(camera.view, clean.view, sizeof(camera.view)) == 0,
          "the view matrix is untouched, so the camera has not moved or turned");
    Check(std::memcmp(camera.eye, clean.eye, sizeof(camera.eye)) == 0, "and neither has the eye");

    float expected[kCameraMatrixFloats];
    Multiply(camera.view, camera.projection, expected);
    for (int i = 0; i < kCameraMatrixFloats; ++i) {
        CheckClose(camera.view_projection[i], expected[i],
                   "the cached view-projection carries the widened frustum");
    }

    // The angle is what a player sets this for. Scaling the ANGLE instead of its
    // tangent is a different operation and lands somewhere else - on this
    // fixture 32.94 degrees goes to 40.55, not to the 41.17 a multiplied angle
    // would give - and the difference only grows towards the wide end.
    const float base = ProjectionFovDegrees(clean.projection[kProjectionVertical], 1.0f);
    const float widened = ProjectionFovDegrees(camera.projection[kProjectionVertical], 1.0f);
    CheckClose(widened,
               2.0f * std::atan(1.25f * std::tan(base * 0.5f * kDegreesToRadians))
                   * kRadiansToDegrees,
               "the widened vertical angle is the scaled half-extent");
    Check(std::fabs(widened - base * 1.25f) > 0.5f, "and is not the angle times the scale");
}

void FieldOfViewAndPoseComposeTogether() {
    std::printf("\nFovScale and a head pose apply to one camera\n");
    Camera camera = PitchedCamera();
    const Camera clean = camera;
    HeadPose pose;
    pose.yaw = 20.0f;
    pose.lean_x = 0.10f;
    Apply(camera, pose, true, 0.8f);

    Camera pose_only = clean;
    Apply(pose_only, pose);
    Check(std::memcmp(camera.view, pose_only.view, sizeof(camera.view)) == 0,
          "the view is exactly what the pose alone produces");
    CheckClose(camera.projection[kProjectionVertical],
               clean.projection[kProjectionVertical] / 0.8f,
               "and the frustum is exactly what the scale alone produces");

    float expected[kCameraMatrixFloats];
    Multiply(camera.view, camera.projection, expected);
    for (int i = 0; i < kCameraMatrixFloats; ++i) {
        CheckClose(camera.view_projection[i], expected[i],
                   "with the cached product built from both");
    }
}

}  // namespace

int main() {
    RotationIsOneToOneAndDoesNotOrbit();
    RotationUsesTheCameraLocalAxes();
    YawTurnsAboutWorldUp();
    CameraLocalYawIsTheOtherPosition();
    LeanMovesAlongTheCleanCameraAxes();
    ViewTranslationAndCachedProductFollowThePose();
    ZeroPoseIsByteExact();
    FieldOfViewScalesTheFrustumOnly();
    FieldOfViewAndPoseComposeTogether();
    return sr_test::Summary("camera transform");
}
