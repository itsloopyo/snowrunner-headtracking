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
        /* player_camera_getter_rva    */ 0x005C1060,
        /* matrix_inverse_rva          */ 0x00DEB280,
        /* frustum_frame_return_rva    */ 0x00AAD0E9,
        /* render_view                 */ 0x000,
        /* render_projection           */ 0x040,
        /* render_eye                  */ 0x0B0,
        /* render_view_projection      */ 0x0C0,
        /* render_inverse_view         */ 0x080,
    },
};

}  // namespace sr_ht::builds
