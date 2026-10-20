// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_profile.h"

// Every Game Pass (GDK) build profile lives here, append-only, and separately
// from the Steam ones. A store variant is a different link of the same source:
// same code, different addresses, its own PE fingerprint. Never merge the two
// files and never edit a shipped profile's numbers.

namespace sr_ht::builds {

// SnowRunner, Microsoft Store / PC Game Pass package
// FocusHomeInteractiveSA.SnowRunnerWindows10 1.0.153.0, Content\SnowRunner.exe
// built 2026-07-22 08:20:09 UTC - five minutes after the Steam build of the
// same day, from the same source.
//
// The exe cannot be read off disk: the Xbox app keeps package content in a
// container only the gaming services stack can open, so these addresses were
// carried over from the Steam profile against a dump of the GDK image taken
// from inside the running process. Each function was matched on its own
// instruction bytes with every linker-movable field (RIP-relative
// displacements, relative branch targets) wildcarded, and each call site on the
// code around the call plus the identity of the callee.
// drive_camera_update_rva is also vtable slot 4 of combineDriveCameraAction,
// which is where the Steam profile's own value came from.
//
// The record layout is unchanged from the Steam build, as a layout from one
// source compile has to be, and was confirmed in the running game.
extern const BuildProfile kGdkProfile_20260722 = {
    "gdk-win64-20260722",
    { 0x6A607D39, 0x02728000, 0x025E3AAD },
    {
        /* drive_camera_update_rva     */ 0x004BB9E0,
        /* render_camera_upload_rva    */ 0x00A069E0,
        /* camera_frustum_rva          */ 0x00A06BB0,
        /* camera_bounds_rva           */ 0x00A06CB0,
        /* camera_view_rays_rva        */ 0x00A06F20,
        /* motion_blur_rays_return_rva */ 0x00595D41,
        /* player_camera_getter_rva    */ 0x005C1060,
        /* matrix_inverse_rva          */ 0x00DEB280,
        /* frustum_frame_return_rva    */ 0x00AAD0E9,
        /* render_view                 */ 0x000,
        /* render_projection           */ 0x040,
        /* render_eye                  */ 0x0B0,
        /* render_view_projection      */ 0x0C0,
        /* render_inverse_view         */ 0x080,
        /* render_vertical_fov         */ 0x108,
    },
};

// Package 1.0.154.0, Content\SnowRunner.exe built 2026-09-24 14:30:56 UTC.
// Carried from gdk-win64-20260722 against a dump of the running image the same
// way, each function and call site a single hit. Startup discovery on this
// image resolves the getter, rays, motion-blur call site, inverse and record
// offsets to the same values, and the drive update is also vtable slot 4 of
// combineDriveCameraAction.
extern const BuildProfile kGdkProfile_20260924 = {
    "gdk-win64-20260924",
    { 0x6AB53420, 0x0272C000, 0x025EB136 },
    {
        /* drive_camera_update_rva     */ 0x004BBA60,
        /* render_camera_upload_rva    */ 0x00A081E0,
        /* camera_frustum_rva          */ 0x00A083B0,
        /* camera_bounds_rva           */ 0x00A084B0,
        /* camera_view_rays_rva        */ 0x00A08720,
        /* motion_blur_rays_return_rva */ 0x00595E41,
        /* player_camera_getter_rva    */ 0x005C1160,
        /* matrix_inverse_rva          */ 0x00DECC90,
        /* frustum_frame_return_rva    */ 0x00AAE8E9,
        /* render_view                 */ 0x000,
        /* render_projection           */ 0x040,
        /* render_eye                  */ 0x0B0,
        /* render_view_projection      */ 0x0C0,
        /* render_inverse_view         */ 0x080,
        /* render_vertical_fov         */ 0x108,
    },
};

}  // namespace sr_ht::builds
