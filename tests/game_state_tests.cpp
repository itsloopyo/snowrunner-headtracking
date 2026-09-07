// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The gate's classification. Reading the engine needs the game; deciding what
// the reading means does not, and that decision is what this locks.
//
// Two of this mod's gates are not represented here because they are not code:
// menus, loading screens and the pause menu are covered by the hook itself -
// combineDriveCameraAction's update is simply not called outside gameplay, and
// that was measured against the running game (the call count stops the instant
// the pause menu opens and resumes on the frame it closes).

#include "game_state.h"

#include "test_support.h"

#include <cstdio>

using namespace sr_ht;
using sr_test::Check;

int main() {
    std::printf("\nthe co-op gate outranks the toggle\n");
    Check(ShouldFollowHead(true, false), "tracking on, no session: the view follows the head");
    Check(!ShouldFollowHead(true, true), "tracking on, co-op session live: the view is left alone");
    Check(!ShouldFollowHead(false, false), "tracking off: the view is left alone");
    Check(!ShouldFollowHead(false, true), "tracking off in co-op: the view is left alone");

    std::printf("\nthe session hold is long enough to bridge an uneven tick\n");
    Check(kSessionHoldSeconds >= 1.0,
          "a session that ticks a few times a second cannot flicker the gate open");

    return sr_test::Summary("game_state");
}
