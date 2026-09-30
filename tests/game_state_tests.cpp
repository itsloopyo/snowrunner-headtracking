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
#include "session_thunks.h"
#include "session_vtable.h"
#include "cameraunlock/hooks/hook_manager.h"

#include <windows.h>

#include "test_support.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace sr_ht;
using sr_test::Check;

namespace sr_ht::detail {
bool WatchSessionMethods(const cameraunlock::memory::VtableInfo& info);
}

namespace {

__declspec(noinline) double MixedArguments(int a, double b, int c, double d,
                                          double e, int f, double g) {
    return a + b * 2 + c * 3 + d * 4 + e * 5 + f * 6 + g * 7;
}

void CheckSessionBoundary() {
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x3000,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (!Check(memory != nullptr, "allocate independent vtable fixture")) return;
    DWORD previous = 0;
    if (!Check(VirtualProtect(memory, 0x1000, PAGE_EXECUTE_READ, &previous) != FALSE,
               "fixture methods occupy executable memory")) {
        VirtualFree(memory, 0, MEM_RELEASE);
        return;
    }
    const auto base = reinterpret_cast<std::uintptr_t>(memory);
    const std::uint32_t locator[] = {1, 0, 0, 0x1800, 0x1900, 0x1000};
    std::memcpy(memory + 0x1000, locator, sizeof(locator));
    cameraunlock::memory::VtableInfo input{};
    input.vfunc_count = 12;
    for (int slot = 0; slot < 12; ++slot) input.vfuncs[slot] = base + 16 * slot;
    input.vfuncs[6] = base + 0x1000;
    auto output = input;
    std::string error;
    Check(BoundSessionVtable(base, 0x3000, output, error) && output.vfunc_count == 6
          && output.vfuncs[6] == 0 && output.vfuncs[11] == 0,
          "stop at adjacent RTTI table before executable entries from another class");
    input.vfuncs[3] = base + 0x1000;
    output = input;
    Check(BoundSessionVtable(base, 0x3000, output, error) && output.vfunc_count == 3,
          "derive moved boundary instead of fixing the historical slot count");
    memory[0x1000] = 0;
    output = input;
    Check(!BoundSessionVtable(base, 0x3000, output, error) && output.vfunc_count == 0
          && output.vfuncs[0] == 0 && !error.empty(),
          "invalid locator rejects and clears an earlier populated result");
    std::memcpy(memory + 0x1000, locator, sizeof(locator));
    input.vfuncs[3] = base + 0x2FF8;
    output = input;
    Check(!BoundSessionVtable(base, 0x3000, output, error), "reject truncated locator");
    input.vfuncs[3] = base + 0x4000;
    output = input;
    Check(!BoundSessionVtable(base, 0x3000, output, error), "reject outside-image method");
    input.vfuncs[3] = base + 48;
    input.vfuncs[6] = base + 96;
    output = input;
    Check(!BoundSessionVtable(base, 0x3000, output, error), "reject missing boundary");
    VirtualFree(memory, 0, MEM_RELEASE);
}

}

int main() {
    CheckSessionBoundary();
    const auto calls = sr_session_calls.load();
    for (unsigned slot = 0; slot < 12; ++slot) {
        sr_session_original[slot] = reinterpret_cast<void*>(&MixedArguments);
        const auto thunk = reinterpret_cast<decltype(&MixedArguments)>(kSessionThunks[slot]);
        Check(thunk(2, 3.5, 5, 7.25, 11.5, 13, 17.25)
              == MixedArguments(2, 3.5, 5, 7.25, 11.5, 13, 17.25),
              "session thunk preserves mixed register/stack arguments and floating return");
    }
    Check(sr_session_calls.load() == calls + 12, "each passthrough records one session call");

    cameraunlock::memory::VtableInfo methods{};
    methods.vfunc_count = 2;
    methods.vfuncs[0] = reinterpret_cast<std::uintptr_t>(&MixedArguments);
    methods.vfuncs[1] = reinterpret_cast<std::uintptr_t>(&methods);
    Check(!detail::WatchSessionMethods(methods), "failed second hook rejects the whole gate");
    auto& hooks = cameraunlock::hooks::HookManager::Instance();
    void* trampoline = nullptr;
    const auto created = hooks.CreateHook(reinterpret_cast<void*>(&MixedArguments),
        reinterpret_cast<void*>(sr_session_thunk_0), &trampoline);
    Check(created == cameraunlock::hooks::HookStatus::Ok,
          "failed gate removed its earlier hook and trampoline");
    if (created == cameraunlock::hooks::HookStatus::Ok)
        hooks.RemoveHook(reinterpret_cast<void*>(&MixedArguments));
    methods.vfuncs[1] = methods.vfuncs[0];
    Check(detail::WatchSessionMethods(methods), "duplicate methods share one hook");
    const auto before_hook_call = sr_session_calls.load();
    volatile auto hooked = &MixedArguments;
    Check(hooked(2, 3.5, 5, 7.25, 11.5, 13, 17.25) == 309.25,
          "real hook preserves native arguments and result");
    Check(sr_session_calls.load() == before_hook_call + 1, "real method hook records the call");
    ShutdownGameState();

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
