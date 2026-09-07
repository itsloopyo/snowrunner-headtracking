// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include "cameraunlock/memory/pe_fingerprint.h"

namespace sr_ht::builds {

// Everything this mod pins to a specific SnowRunner.exe build.
//
struct OffsetTable {
    // combineDriveCameraAction::Update. It runs only while a vehicle camera is
    // active, including both exterior and cabin views, so its recent activity
    // gates render injection out of menus and loading screens.
    unsigned int drive_camera_update_rva;

    // Uploads one render camera's matrices and eye into the frame's constant
    // buffer. The two return addresses are the player-view calls measured in
    // both exterior and cabin views; the same uploader also handles shadows and
    // reflections, which must remain untouched.
    unsigned int render_camera_upload_rva;
    unsigned int render_primary_return_rva;
    unsigned int render_secondary_return_rva;
    unsigned int camera_frustum_rva;
    unsigned int player_camera_getter_rva;
    unsigned int matrix_inverse_rva;
    unsigned int frustum_frame_return_rva;

    // Byte offsets in the render-camera source record. The view is row-major;
    // its first three columns are world-space right, up and forward, and the
    // cached matrix is view * projection.
    unsigned int render_view;
    unsigned int render_projection;
    unsigned int render_eye;
    unsigned int render_view_projection;
    unsigned int render_inverse_view;
};

struct BuildProfile {
    const char* Name;
    cameraunlock::memory::PeFingerprint Fingerprint;
    OffsetTable Offsets;
};

// A profile with no camera addresses is a placeholder landed ahead of the
// rederive: the fingerprint routes, but the mod must stay dormant.
//
inline bool IsProfileComplete(const BuildProfile& p) {
    return p.Offsets.drive_camera_update_rva != 0
        && p.Offsets.render_camera_upload_rva != 0
        && p.Offsets.render_primary_return_rva != 0
        && p.Offsets.render_secondary_return_rva != 0
        && p.Offsets.camera_frustum_rva != 0
        && p.Offsets.player_camera_getter_rva != 0
        && p.Offsets.matrix_inverse_rva != 0
        && p.Offsets.frustum_frame_return_rva != 0
        && p.Offsets.render_projection != 0
        && p.Offsets.render_eye != 0
        && p.Offsets.render_view_projection != 0
        && p.Offsets.render_inverse_view != 0;
}

}  // namespace sr_ht::builds
