// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <utility>

#include "logging.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/memory/rtti_vtable.h"

namespace sr_ht {

namespace {

// The engine's own multiplayer session class. Saber3D ships full MSVC RTTI, so
// this resolves by name at runtime and nothing about the session has to be
// pinned to a build.
constexpr char kSessionClass[] = "netREDSTONE_SESSION";

// Every slot the RTTI walk reports gets a detour. Which one ticks a live
// session is not knowable without a session to watch, and it does not matter:
// the question is only whether the class is doing anything at all.
constexpr int kMaxSessionSlots = 12;

using SessionFn = std::uintptr_t(__fastcall*)(void*, void*, void*, void*);
SessionFn g_original[kMaxSessionSlots];
void* g_target[kMaxSessionSlots];
int g_slots = 0;

// Bumped by the detours, read by the gate. A COUNT rather than a timestamp, and
// that is the whole point: these detours stand in for twelve virtual functions
// whose signatures are not known, so the thunk must disturb as little as
// possible before tail-calling the original. Reading the clock means calling
// QueryPerformanceCounter, and every XMM register a float or double argument
// would have arrived in is volatile across that call - a hooked slot taking a
// float delta-time would receive garbage. A lock-xadd on an integer touches no
// XMM register at all. The clock is read by the gate instead, on the mod's own
// thread, where clobbering nothing but our own registers costs nothing.
std::atomic<unsigned long long> g_session_calls{0};

// The hold window in performance-counter ticks, resolved once in InitGameState.
long long g_session_hold_ticks = 0;

long long NowTicks() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

template <int Slot>
std::uintptr_t __fastcall SessionDetour(void* self, void* a2, void* a3, void* a4) {
    g_session_calls.fetch_add(1, std::memory_order_relaxed);
    return g_original[Slot](self, a2, a3, a4);
}

template <int... Slot>
void FillDetours(void* (&out)[kMaxSessionSlots], std::integer_sequence<int, Slot...>) {
    ((out[Slot] = reinterpret_cast<void*>(&SessionDetour<Slot>)), ...);
}

}  // namespace

bool InitGameState() {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;

    cameraunlock::memory::VtableInfo info{};
    if (!cameraunlock::memory::FindVtableFromRTTI(GetModuleHandleW(nullptr), kSessionClass, info,
                                                  kMaxSessionSlots)) {
        Log::Line("[state] %s has no vtable in this build - the multiplayer gate cannot be "
                  "resolved.", kSessionClass);
        return false;
    }

    void* detours[kMaxSessionSlots]{};
    FillDetours(detours, std::make_integer_sequence<int, kMaxSessionSlots>{});

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    g_session_hold_ticks =
        static_cast<long long>(kSessionHoldSeconds * static_cast<double>(frequency.QuadPart));

    HookManager& hooks = HookManager::Instance();
    const HookStatus initialized = hooks.Initialize();
    if (initialized != HookStatus::Ok && initialized != HookStatus::ErrorAlreadyInitialized) {
        Log::Line("[state] MinHook init failed: %s",
                  cameraunlock::hooks::HookStatusToString(initialized));
        return false;
    }

    for (int slot = 0; slot < info.vfunc_count && slot < kMaxSessionSlots; ++slot) {
        void* target = reinterpret_cast<void*>(info.vfuncs[slot]);
        const HookStatus created =
            hooks.CreateHook(target, detours[slot], reinterpret_cast<void**>(&g_original[slot]));
        if (created != HookStatus::Ok) {
            // Named rather than counted. Which slots went unwatched is the
            // difference between a gate that closes in co-op and one that never
            // does, so a silent skip here would be the fault hiding behind the
            // aggregate "watching N of M" line below.
            Log::Line("[state]   slot %d could not be hooked: %s", slot,
                      cameraunlock::hooks::HookStatusToString(created));
            continue;
        }
        // A created-but-not-enabled hook still holds a MinHook entry and its
        // trampoline, and ShutdownGameState only walks the slots that made it
        // into g_target. Without this, a slot that creates and fails to enable
        // is never given back - including on the path where none of them enable
        // and the mod reports the gate unresolvable and goes dormant.
        const HookStatus enabled = hooks.EnableHook(target);
        if (enabled != HookStatus::Ok) {
            Log::Line("[state]   slot %d could not be enabled: %s", slot,
                      cameraunlock::hooks::HookStatusToString(enabled));
            hooks.RemoveHook(target);
            continue;
        }
        g_target[slot] = target;
        ++g_slots;
    }

    if (g_slots == 0) {
        Log::Line("[state] none of %s's %d vtable slots could be watched - the multiplayer gate "
                  "cannot be resolved.", kSessionClass, info.vfunc_count);
        return false;
    }

    Log::Line("[state] watching %d of %s's %d vtable slots for a live co-op session",
              g_slots, kSessionClass, info.vfunc_count);
    return true;
}

bool IsMultiplayerSessionLive() {
    // Called from the camera update, which is where the clock is read: the
    // detours only count, so that they clobber nothing on the way to the
    // original.
    //
    // The consequence, stated because it is a real difference from stamping the
    // time inside the detour: the hold is measured from when this gate NOTICED
    // the count move, not from when the session ticked. The camera update stops
    // being called in menus and on loading screens, so a session that ends
    // during one is noticed on the first frame back in gameplay and holds the
    // gate shut for kSessionHoldSeconds from there. That delays tracking coming
    // back; it never lets it in early, which is the direction that matters.
    static unsigned long long seen_calls = 0;
    static long long seen_at = 0;

    const unsigned long long calls = g_session_calls.load(std::memory_order_relaxed);
    if (calls != seen_calls) {
        seen_calls = calls;
        seen_at = NowTicks();
    }
    if (seen_at == 0) return false;
    return (NowTicks() - seen_at) < g_session_hold_ticks;
}

void ShutdownGameState() {
    cameraunlock::hooks::HookManager& hooks = cameraunlock::hooks::HookManager::Instance();
    for (int slot = 0; slot < kMaxSessionSlots; ++slot) {
        if (g_target[slot] == nullptr) continue;
        hooks.DisableHook(g_target[slot]);
        hooks.RemoveHook(g_target[slot]);
        g_target[slot] = nullptr;
    }
    g_slots = 0;
}

bool ShouldFollowHead(bool tracking_enabled, bool multiplayer_session_live) {
    return tracking_enabled && !multiplayer_session_live;
}

}  // namespace sr_ht
