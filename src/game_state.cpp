// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "game_state.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <string>

#include "logging.h"
#include "session_thunks.h"
#include "session_vtable.h"
#include "cameraunlock/memory/pattern_scanner.h"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/memory/rtti_vtable.h"

extern "C" {
std::atomic<unsigned long long> sr_session_calls{0};
void* sr_session_original[12]{};
}
static_assert(std::atomic<unsigned long long>::is_always_lock_free
              && sizeof(sr_session_calls) == 8);

namespace sr_ht {
namespace detail {
bool WatchSessionMethods(const cameraunlock::memory::VtableInfo& info);
}

namespace {

// The engine's own multiplayer session class. Saber3D ships full MSVC RTTI, so
// this resolves by name at runtime and nothing about the session has to be
// pinned to a build.
constexpr char kSessionClass[] = "netREDSTONE_SESSION";

constexpr int kMaxSessionSlots = 12;
void* g_target[kMaxSessionSlots]{};
int g_slots = 0;

// The hold window in performance-counter ticks, resolved once in InitGameState.
long long g_session_hold_ticks = 0;

long long NowTicks() {
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return now.QuadPart;
}

}  // namespace

bool InitGameState() {

    cameraunlock::memory::VtableInfo info{};
    if (!cameraunlock::memory::FindVtableFromRTTI(GetModuleHandleW(nullptr), kSessionClass, info,
                                                  kMaxSessionSlots)) {
        Log::Line("[state] %s has no vtable in this build - the multiplayer gate cannot be "
                  "resolved.", kSessionClass);
        return false;
    }

    std::uintptr_t base = 0;
    std::size_t size = 0;
    std::string error;
    if (!cameraunlock::memory::GetModuleRange(GetModuleHandleW(nullptr), base, size)) {
        Log::Line("[state] cannot read the game image bounds");
        return false;
    }
    if (!BoundSessionVtable(base, size, info, error)) {
        Log::Line("[state] session vtable rejected: %s", error.c_str());
        return false;
    }

    LARGE_INTEGER frequency{};
    QueryPerformanceFrequency(&frequency);
    g_session_hold_ticks =
        static_cast<long long>(kSessionHoldSeconds * static_cast<double>(frequency.QuadPart));

    return detail::WatchSessionMethods(info);
}

namespace detail {
bool WatchSessionMethods(const cameraunlock::memory::VtableInfo& info) {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    HookManager& hooks = HookManager::Instance();
    const HookStatus initialized = hooks.Initialize();
    if (initialized != HookStatus::Ok && initialized != HookStatus::ErrorAlreadyInitialized) {
        Log::Line("[state] MinHook init failed: %s",
                  cameraunlock::hooks::HookStatusToString(initialized));
        return false;
    }

    for (int slot = 0; slot < info.vfunc_count; ++slot) {
        void* target = reinterpret_cast<void*>(info.vfuncs[slot]);
        bool duplicate = false;
        for (int previous = 0; previous < slot; ++previous)
            duplicate |= g_target[previous] == target;
        if (duplicate) continue;
        const HookStatus created = hooks.CreateHook(
            target, reinterpret_cast<void*>(kSessionThunks[slot]), &sr_session_original[slot]);
        if (created != HookStatus::Ok) {
            Log::Line("[state] slot %d could not be hooked: %s", slot,
                      cameraunlock::hooks::HookStatusToString(created));
            ShutdownGameState();
            return false;
        }
        g_target[slot] = target;
        ++g_slots;
    }
    for (int slot = 0; slot < info.vfunc_count; ++slot) {
        if (!g_target[slot]) continue;
        const HookStatus enabled = hooks.EnableHook(g_target[slot]);
        if (enabled != HookStatus::Ok) {
            Log::Line("[state] slot %d could not be enabled: %s", slot,
                      cameraunlock::hooks::HookStatusToString(enabled));
            ShutdownGameState();
            return false;
        }
    }

    Log::Line("[state] watching %d of %s's %d vtable slots for a live co-op session",
              g_slots, kSessionClass, info.vfunc_count);
    return true;
}
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

    const unsigned long long calls = sr_session_calls.load(std::memory_order_relaxed);
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
