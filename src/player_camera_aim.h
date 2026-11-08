// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace sr_ht {

// Whether this frame's player camera was aimed through its LookAt.
//
// While driving, the game aims the player's render record through the record's
// LookAt exactly once per drawn frame, before that frame's visibility pass:
// measured one call per frame from 8 to 171 fps, in the chase and the cabin
// view, while the drive camera itself updates on a 60 Hz tick. The pause menu
// and the map make no call at all (none at up to 433 fps in the map). The map
// draws through the same record from a camera of its own above a scale model of
// the region, and the drive camera keeps updating underneath it, so the vehicle
// gate alone stays open there and the head pose was composed into the map view,
// which then no longer matched the route and markers drawn over it.
//
// A load also aims the record once, from another caller, on the title screen
// where no drive camera runs; the vehicle gate is what keeps that frame clean.
class PlayerCameraAimGate {
public:
    // From the LookAt detour, when the record it aims is the player's.
    void OnAim() { aims_.fetch_add(1, std::memory_order_release); }

    // Once per frame, at the visibility pass, before anything in that frame asks Active().
    void OnFrame() {
        const std::uint64_t aims = aims_.load(std::memory_order_acquire);
        active_ = aims != seen_;
        seen_ = aims;
    }

    bool Active() const { return active_; }

private:
    std::atomic<std::uint64_t> aims_{0};
    std::uint64_t seen_ = 0;
    bool active_ = false;
};

// The render record's LookAt(record, eye, target, up) begins by saving its
// registers and subtracting the eye from the target, component by component:
//
//   mov rax,rsp; mov [rax+8],rbx; mov [rax+10h],rbp; mov [rax+18h],rsi; push rdi;
//   sub rsp,40h; movss xmm0,[r8]; mov rbp,rdx; subss xmm0,[rdx];
//   movss xmm1,[r8+4]; mov rdi,rcx; subss ...
//
// These bytes hold no relative displacement and are identical in every build
// profiled, so a pinned address that has drifted onto something else is refused
// before it is hooked.
inline constexpr std::size_t kLookAtCheckedBytes = 44;

inline bool CheckLookAt(const std::uint8_t* look_at, std::string& error) {
    static const std::uint8_t kHead[kLookAtCheckedBytes] = {
        0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18,
        0x57, 0x48, 0x83, 0xEC, 0x40, 0xF3, 0x41, 0x0F, 0x10, 0x00, 0x48, 0x8B, 0xEA, 0xF3, 0x0F,
        0x5C, 0x02, 0xF3, 0x41, 0x0F, 0x10, 0x48, 0x04, 0x48, 0x8B, 0xF9, 0xF3, 0x0F, 0x5C};
    if (std::memcmp(look_at, kHead, sizeof(kHead)) != 0) {
        error = "the render camera LookAt does not subtract its eye from its target";
        return false;
    }
    return true;
}

}  // namespace sr_ht
