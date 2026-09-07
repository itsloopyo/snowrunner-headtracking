// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_profile.h"

// Every Steam build profile lives here, append-only. Never edit an existing
// profile's numbers to "fix" a patch and never delete one: a user who has held
// back on an older build must keep matching their old profile from the same
// mod binary. Adding a profile is the only correct response to a patch.

namespace sr_ht::builds {

// SnowRunner, Steam app 1465360, Sources\Bin\SnowRunner.exe built
// 2026-07-22 08:15:01 UTC.
//
// Every address is relative to the module as the loader maps it. The render
// record layout and both player-view call sites were measured in the running
// game in exterior and cabin views.
extern const BuildProfile kSteamProfile_20260722 = {
    "steam-win64-20260722",
    { 0x6A607C05, 0x02E02000, 0x02C9F69B },
    {
        /* drive_camera_update_rva     */ 0x00A17740,
        /* render_camera_upload_rva    */ 0x00DA2D40,
        /* render_primary_return_rva   */ 0x00ABF137,
        /* render_secondary_return_rva */ 0x00AC1194,
        /* camera_frustum_rva          */ 0x00DA2F10,
        /* player_camera_getter_rva    */ 0x00AD5EF0,
        /* matrix_inverse_rva          */ 0x01153480,
        /* frustum_frame_return_rva    */ 0x00E0D9E1,
        /* render_view                 */ 0x000,
        /* render_projection           */ 0x040,
        /* render_eye                  */ 0x0B0,
        /* render_view_projection      */ 0x0C0,
        /* render_inverse_view         */ 0x080,
    },
};

}  // namespace sr_ht::builds
