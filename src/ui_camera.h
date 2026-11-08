// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace sr_ht {

// The HUD's world markers are projected by ui3D_CAMERA_MR through a camera
// record its slot 1 fetches once a frame and stores in the object:
//
//   push rbx; sub rsp,20h; mov rbx,rcx; call <snapshot getter>; mov [rbx+N],rax
//
// The snapshot getter reads the second record of the object the player camera
// getter reads the first of, through the same accessor:
//
//   sub rsp,28h; call <accessor>; mov rax,[rax]; mov rax,[rax+10h]   (snapshot)
//   sub rsp,28h; call <accessor>; mov rax,[rax]; mov rax,[rax+8]     (player)
//
// Before the mod redirects that stored pointer, this checks all of it against
// the code, so a pinned address that has drifted onto something else is refused
// rather than written through. `refresh` and `player_getter` point at the
// mapped functions.
inline bool CheckUiCameraRefresh(const std::uint8_t* refresh, const std::uint8_t* player_getter,
                                 unsigned record_offset, std::string& error) {
    auto rel_target = [](const std::uint8_t* call) {
        std::int32_t rel;
        std::memcpy(&rel, call + 1, sizeof(rel));
        return call + 5 + rel;
    };
    static const std::uint8_t kRefreshHead[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8};
    static const std::uint8_t kGetterHead[] = {0x48, 0x83, 0xEC, 0x28, 0xE8};
    static const std::uint8_t kGetterLoad[] = {0x48, 0x8B, 0x00, 0x48, 0x8B, 0x40};

    if (std::memcmp(refresh, kRefreshHead, sizeof(kRefreshHead)) != 0) {
        error = "the HUD camera refresh does not start with push/sub/mov/call";
        return false;
    }
    const std::uint8_t* const store = refresh + sizeof(kRefreshHead) + 4;
    if (store[0] != 0x48 || store[1] != 0x89 || store[2] != 0x43) {
        error = "the HUD camera refresh does not store its call's result into the object";
        return false;
    }
    if (store[3] != record_offset) {
        char message[96];
        std::snprintf(message, sizeof(message),
                      "the HUD camera refresh stores at +0x%X, not the profile's +0x%X",
                      store[3], record_offset);
        error = message;
        return false;
    }

    const std::uint8_t* const snapshot_getter = rel_target(refresh + sizeof(kRefreshHead) - 1);
    auto check_getter = [&](const std::uint8_t* getter, std::uint8_t member, const char* what,
                            const std::uint8_t*& accessor) {
        if (std::memcmp(getter, kGetterHead, sizeof(kGetterHead)) != 0 ||
            std::memcmp(getter + 9, kGetterLoad, sizeof(kGetterLoad)) != 0 || getter[15] != member) {
            error = std::string("the ") + what + " does not read the camera holder's member";
            return false;
        }
        accessor = rel_target(getter + 4);
        return true;
    };
    const std::uint8_t* snapshot_accessor = nullptr;
    const std::uint8_t* player_accessor = nullptr;
    if (!check_getter(snapshot_getter, 0x10, "HUD camera getter", snapshot_accessor)) return false;
    if (!check_getter(player_getter, 0x08, "player camera getter", player_accessor)) return false;
    if (snapshot_accessor != player_accessor) {
        error = "the HUD camera and the player camera come from different holders";
        return false;
    }
    return true;
}

}  // namespace sr_ht
