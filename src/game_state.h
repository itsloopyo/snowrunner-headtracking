// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace sr_ht {

// Resolves the multiplayer session class and watches it. Returns false when the
// class could not be found, which leaves the mod refusing to track rather than
// tracking into a co-op session it cannot see.
bool InitGameState();

// True while a co-op session looks live.
//
// SnowRunner's own network session object is a netREDSTONE_SESSION, and the mod
// detours every entry in that class's vtable it can. A call means a session
// object is running, and the gate stays shut for kSessionHoldSeconds after the
// last one so a session that ticks unevenly cannot flicker the view.
//
// What has been measured is only one direction: across a normal single-player
// session not one slot was ever called. The gate has never been driven through
// a live co-op session, so "no calls" is being read as "no session" on the
// strength of the single-player observation alone. Silence therefore leaves
// tracking ON, and a session whose object is a derived class with its own
// vtable, or whose ticking slot is one of the slots that failed to hook, would
// be silent. InitGameState logs which slots it is watching for that reason.
bool IsMultiplayerSessionLive();

// Frees the session detours. Safe to call when InitGameState failed.
void ShutdownGameState();

// How long after the last sign of a network session the gate stays shut.
constexpr double kSessionHoldSeconds = 5.0;

// Whether tracking should follow the head, given the toggle and the session
// state. Pure, so the classification is testable without a game: reading the
// engine is InitGameState's job, deciding what it means is this.
bool ShouldFollowHead(bool tracking_enabled, bool multiplayer_session_live);

}  // namespace sr_ht
