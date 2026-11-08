// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The HUD camera refresh check against synthetic code laid out the way every
// shipped build has it, and against each way a drifted address could differ.

#include "ui_camera.h"

#include "test_support.h"

#include <cstdint>
#include <cstring>
#include <string>

using sr_ht::CheckUiCameraRefresh;
using sr_test::Check;

namespace {

constexpr int kPlayerGetter = 0x00;
constexpr int kSnapshotGetter = 0x20;
constexpr int kRefresh = 0x40;
constexpr int kAccessor = 0x100;
constexpr int kOtherAccessor = 0x140;

struct Image {
    std::uint8_t bytes[0x200];

    Image() {
        std::memset(bytes, 0xCC, sizeof(bytes));
        Getter(kPlayerGetter, kAccessor, 0x08);
        Getter(kSnapshotGetter, kAccessor, 0x10);
        static const std::uint8_t head[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8};
        std::memcpy(bytes + kRefresh, head, sizeof(head));
        Rel(kRefresh + 9, kSnapshotGetter);
        static const std::uint8_t store[] = {0x48, 0x89, 0x43, 0x18};
        std::memcpy(bytes + kRefresh + 14, store, sizeof(store));
        bytes[kAccessor] = 0xC3;
        bytes[kOtherAccessor] = 0xC3;
    }

    void Rel(int call, int target) {
        const std::int32_t rel = target - (call + 5);
        std::memcpy(bytes + call + 1, &rel, sizeof(rel));
    }

    void Getter(int at, int accessor, std::uint8_t member) {
        static const std::uint8_t head[] = {0x48, 0x83, 0xEC, 0x28, 0xE8};
        std::memcpy(bytes + at, head, sizeof(head));
        Rel(at + 4, accessor);
        const std::uint8_t load[] = {0x48, 0x8B, 0x00, 0x48, 0x8B, 0x40, member};
        std::memcpy(bytes + at + 9, load, sizeof(load));
    }

    bool Check(unsigned offset, std::string& error) const {
        return CheckUiCameraRefresh(bytes + kRefresh, bytes + kPlayerGetter, offset, error);
    }
};

void AcceptsTheShippedShape() {
    Image image;
    std::string error;
    Check(image.Check(0x18, error), "the shipped shape with record offset 0x18 is accepted");
}

void RefusesAnotherRecordOffset() {
    Image image;
    std::string error;
    Check(!image.Check(0x20, error), "a profile offset the store does not use is refused");
    Check(error.find("+0x18") != std::string::npos, "the refusal names the offset the code uses");
}

void RefusesAnotherFunction() {
    Image image;
    image.bytes[kRefresh] = 0x48;
    std::string error;
    Check(!image.Check(0x18, error), "a refresh that does not start push/sub/mov/call is refused");
}

void RefusesAGetterReadingAnotherMember() {
    Image image;
    image.Getter(kSnapshotGetter, kAccessor, 0x08);
    std::string error;
    Check(!image.Check(0x18, error), "a HUD getter reading the player record is refused");
}

void RefusesAnotherHolder() {
    Image image;
    image.Getter(kSnapshotGetter, kOtherAccessor, 0x10);
    std::string error;
    Check(!image.Check(0x18, error), "a HUD camera from a different holder is refused");
}

void RefusesAPlayerGetterOfAnotherShape() {
    Image image;
    image.bytes[kPlayerGetter + 15] = 0x10;
    std::string error;
    Check(!image.Check(0x18, error), "a player getter that is not the +8 read is refused");
}

}  // namespace

int main() {
    AcceptsTheShippedShape();
    RefusesAnotherRecordOffset();
    RefusesAnotherFunction();
    RefusesAGetterReadingAnotherMember();
    RefusesAnotherHolder();
    RefusesAPlayerGetterOfAnotherShape();
    return sr_test::Summary("ui camera");
}
