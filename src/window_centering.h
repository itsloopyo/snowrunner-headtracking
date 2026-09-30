// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace sr_ht {

// Watches for settled window sizes during the first minute of startup.
// Blocks the bootstrap thread after hooks, receiver and hotkeys are ready.
void CenterWindowWhenReady();

}  // namespace sr_ht
