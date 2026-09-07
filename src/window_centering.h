// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace sr_ht {

// Waits for the game to bring its window up and hold it still, then centres it
// on the work area of the monitor it opened on. A window the game centred
// itself, and a fullscreen or borderless one that already fills the screen, are
// left where they are - so this only ever moves a windowed-mode game.
//
// Blocks for as long as it takes the window to settle, so it is the last thing
// the bootstrap does: the hook, the receiver and the hotkeys are all up before
// this waits on anything.
void CenterWindowWhenReady();

}  // namespace sr_ht
