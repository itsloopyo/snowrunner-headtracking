// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cmath>

#include "camera_transform.h"

namespace sr_ht {

// The renderer's projection is row-major and pairs with the row-vector view
// matrix beside it in the same record, so element 0 holds 1/tan(half the
// horizontal angle) and element 5 holds 1/tan(half the vertical angle).
constexpr int kProjectionHorizontal = 0;
constexpr int kProjectionVertical = 5;

constexpr float kRadiansToDegrees = 57.29577951f;

// Widens or narrows the frustum the way a field of view slider does: the scale
// multiplies its half-extent, so 1.25 puts a quarter more of the world across
// the frame at every distance, and both axes keep the aspect the game built.
//
// It is the TANGENT that is scaled, not the angle. Scaling the angle is not the
// same operation and stops meaning anything near the ends - twice 90 degrees is
// 180, which no perspective projection can draw, while twice its half-extent is
// 127 degrees and draws fine. Both terms hold the RECIPROCAL of that tangent,
// which is why widening divides them.
inline void ScaleProjectionFieldOfView(float projection[kCameraMatrixFloats], float scale) {
    projection[kProjectionHorizontal] /= scale;
    projection[kProjectionVertical] /= scale;
}

// The angle one of those two terms spans, in degrees, once `scale` has been
// applied to it. Pass 1.0 for the angle the game itself asked for.
inline float ProjectionFovDegrees(float term, float scale) {
    return 2.0f * std::atan(scale / term) * kRadiansToDegrees;
}

// Whether the two terms are numbers worth reporting as an angle. Nothing
// validates them: they come out of the game's own camera record at the offset
// the build profile pins, so a profile whose projection offset no longer lands
// on a perspective matrix reads back whatever is there. Reporting that as a
// field of view would send a bug report chasing the wrong thing.
inline bool IsReportableProjection(const float projection[kCameraMatrixFloats]) {
    return std::isfinite(projection[kProjectionHorizontal])
        && projection[kProjectionHorizontal] > 0.0f
        && std::isfinite(projection[kProjectionVertical])
        && projection[kProjectionVertical] > 0.0f;
}

}  // namespace sr_ht
