// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

// There is deliberately no DLL_PROCESS_DETACH case.
//
// Initialize pins the module, so an explicit FreeLibrary cannot unmap it and
// the detach-with-lpReserved-null path never runs; the mod refuses to install
// anything at all if the pin failed. That leaves process exit, where the kernel
// has already killed every other thread without unwinding - undoing hooks or
// joining threads there is a deadlock, not a tidy-up. The OS reclaims the
// sockets, threads and heap.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        sr_ht::Initialize();
    }
    return TRUE;
}
