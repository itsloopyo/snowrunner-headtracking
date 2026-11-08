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
    // buffer. Which of its calls belong to the player's view is decided from the
    // camera it is handed rather than from where it was called, so no call site
    // is pinned here; the same uploader also serves shadows and light, which get
    // a camera of their own and must remain untouched.
    unsigned int render_camera_upload_rva;
    unsigned int camera_frustum_rva;
    unsigned int camera_bounds_rva;
    unsigned int camera_view_rays_rva;
    unsigned int motion_blur_rays_return_rva;
    unsigned int player_camera_getter_rva;
    unsigned int matrix_inverse_rva;
    unsigned int frustum_frame_return_rva;

    // A second uploader writing the same constants from a camera record. The
    // main draw calls it to put the player's camera back after each extra view
    // it renders (a list of 0x138-byte camera entries), so the passes after
    // that are drawn from whatever record it is handed.
    unsigned int render_camera_upload_alt_rva;

    // ui3D_CAMERA_MR's vtable slot 1. Once a frame it stores the camera record
    // the HUD projects world markers through, a snapshot of the player camera
    // the main loop copies each frame, at ui_camera_record in the object.
    unsigned int ui_camera_refresh_rva;

    // Byte offsets in the render-camera source record. The view is row-major;
    // its first three columns are world-space right, up and forward, and the
    // cached matrix is view * projection.
    unsigned int render_view;
    unsigned int render_projection;
    unsigned int render_eye;
    unsigned int render_view_projection;
    unsigned int render_inverse_view;
    unsigned int render_vertical_fov;

    unsigned int ui_camera_record;
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
        && p.Offsets.camera_frustum_rva != 0
        && p.Offsets.camera_bounds_rva != 0
        && p.Offsets.camera_view_rays_rva != 0
        && p.Offsets.motion_blur_rays_return_rva != 0
        && p.Offsets.player_camera_getter_rva != 0
        && p.Offsets.matrix_inverse_rva != 0
        && p.Offsets.frustum_frame_return_rva != 0
        && p.Offsets.render_camera_upload_alt_rva != 0
        && p.Offsets.ui_camera_refresh_rva != 0
        && p.Offsets.render_projection != 0
        && p.Offsets.render_eye != 0
        && p.Offsets.render_view_projection != 0
        && p.Offsets.render_inverse_view != 0
        && p.Offsets.render_vertical_fov != 0
        && p.Offsets.ui_camera_record != 0;
}

}  // namespace sr_ht::builds
