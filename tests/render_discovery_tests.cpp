// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/render_discovery.h"
#include "test_support.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <vector>

using sr_ht::builds::DiscoverMotionBlur;
using sr_ht::builds::MotionBlurDiscovery;
using sr_test::Check;

namespace {

struct Fixture {
    std::vector<std::uint8_t> image;
    std::vector<std::array<std::uint32_t, 3>> functions;
    std::uint32_t shift, cursor = 0, name, masked, half, global;
    std::uint32_t getter, rays, inverse, pass, caller = 0, fov, inv, overwrite = 0;

    explicit Fixture(std::uint32_t relocation = 0, std::uint32_t field = 0x240)
        : image(0xA000 + relocation), shift(relocation), name(0x6000 + shift), masked(name + 32),
          half(name + 64), global(0x7000 + shift), getter(0x1200 + shift), rays(0x1300 + shift),
          inverse(0x1500 + shift), pass(0x1100 + shift), fov(field), inv(field - 0x100) {
        Put<std::uint16_t>(0, 0x5A4D);
        Put<std::uint32_t>(0x3C, 0x80);
        Put<std::uint32_t>(0x80, 0x4550);
        Put<std::uint16_t>(0x84, 0x8664);
        Put<std::uint16_t>(0x86, 3);
        Put<std::uint16_t>(0x94, 0xF0);
        Put<std::uint16_t>(0x98, 0x20B);
        Put<std::uint32_t>(0xD0, static_cast<std::uint32_t>(image.size()));
        Put<std::uint32_t>(0x104, 16);
        Put<std::uint32_t>(0x120, 0x9000 + shift);
        Section(0x188, 0x1000 + shift, 0x4000, 0x60000020);
        Section(0x1B0, 0x6000 + shift, 0x2000, 0xC0000040);
        Section(0x1D8, 0x9000 + shift, 0x1000, 0x40000040);
        std::memcpy(image.data() + name, "MotionBlur", 11);
        std::memcpy(image.data() + masked, "MotionBlurMSStencilMasked", 25);
        Put<float>(half, 0.5f);
        EmitPass(pass);
        cursor = getter;
        Bytes({0x48, 0x83, 0xEC, 0x28});
        Call(0x1600 + shift);
        Bytes({0x48, 0x8B, 0x40, 0x10, 0x48, 0x8B, 0x40, 0x20, 0x48, 0x83, 0xC4, 0x28, 0xC3});
        Function(getter);
        cursor = 0x1600 + shift;
        Rip({0x48, 0x8D, 0x05}, global);
        Bytes({0xC3});
        cursor = inverse;
        Bytes({0xC3});
        Function(inverse);
        cursor = 0x1700 + shift;
        Bytes({0xC3});
        Function(0x1700 + shift);
        cursor = rays;
        Bytes({0xF3, 0x0F, 0x10, 0x81}); Dword(fov);
        Bytes({0x48, 0x8B, 0xF2});
        Rip({0xF3, 0x0F, 0x59, 0x05}, half);
        Bytes({0x48, 0x8B, 0xF9});
        Call(0x1700 + shift);
        Bytes({0xF3, 0x0F, 0x59, 0xA7}); Dword(fov + 4);
        Bytes({0x41, 0xBF}); Dword(4);
        const auto loop = cursor;
        Bytes({0x83, 0xBF}); Dword(inv); Bytes({0xFF, 0x75});
        const auto skip = cursor++;
        Bytes({0x4C, 0x8B, 0xC7, 0x48, 0x8D, 0x8F}); Dword(inv);
        Bytes({0x33, 0xD2});
        Call(inverse);
        image[skip] = static_cast<std::uint8_t>(cursor - skip - 1);
        Bytes({0x4C, 0x8D, 0x87}); Dword(inv);
        Bytes({0x0F, 0x11, 0x1E, 0x48, 0x83, 0xC6, 0x10, 0x0F, 0x11, 0x5D, 7,
               0x49, 0x83, 0xEF, 1, 0x0F, 0x85});
        Dword(loop - cursor - 4);
        Bytes({0xC3});
        Function(rays);
        Finish();
    }

    template <class T> void Put(std::uint32_t offset, T value) {
        std::memcpy(image.data() + offset, &value, sizeof(value));
    }
    void Section(std::uint32_t at, std::uint32_t begin, std::uint32_t size, std::uint32_t flags) {
        Put(at + 8, size); Put(at + 12, begin); Put(at + 36, flags);
    }
    void Bytes(std::initializer_list<std::uint8_t> bytes) {
        for (auto byte : bytes) image[cursor++] = byte;
    }
    void Dword(std::uint32_t value) { Put(cursor, value); cursor += 4; }
    void Rip(std::initializer_list<std::uint8_t> prefix, std::uint32_t target) {
        Bytes(prefix); Dword(target - cursor - 4);
    }
    void Call(std::uint32_t target) { Rip({0xE8}, target); }
    void Function(std::uint32_t begin) {
        const auto unwind = 0x9800 + shift + static_cast<std::uint32_t>(functions.size()) * 16;
        Put<std::uint8_t>(unwind, 1);
        functions.push_back({begin, cursor, unwind});
    }
    void EmitPass(std::uint32_t begin) {
        cursor = begin;
        Rip({0x4C, 0x8D, 0x05}, name);
        Rip({0x48, 0x8D, 0x05}, masked);
        Call(getter);
        Bytes({0x48, 0x8B, 0xF0});
        overwrite = cursor;
        Bytes({0x90, 0x90, 0x90, 0x48, 0x8D, 0x55, 0xC0, 0x48, 0x8B, 0xCE});
        Call(rays);
        caller = cursor;
        Bytes({0x4C, 0x89, 0x6C, 0x24, 0x20, 0x41, 0xB9}); Dword(4);
        Bytes({0x4C, 0x8D, 0x45, 0xC0, 0xBA}); Dword(42);
        Bytes({0x48, 0x8B, 0xCB}); Call(0x1700 + shift);
        Bytes({0xC3});
        Function(begin);
    }
    void Finish() {
        std::sort(functions.begin(), functions.end());
        auto entry = 0x9000 + shift;
        for (const auto& function : functions)
            for (auto field : function) { Put(entry, field); entry += 4; }
        Put<std::uint32_t>(0x124, static_cast<std::uint32_t>(functions.size()) * 12);
    }
};

void Resolved(const Fixture& f, MotionBlurDiscovery& result) {
    std::string error;
    const bool ok = DiscoverMotionBlur(f.image.data(), f.image.size(), result, error);
    if (!ok) std::printf("discovery: %s\n", error.c_str());
    Check(ok, "named graph resolves");
    Check(result.player_camera_getter == f.getter && result.view_rays == f.rays
          && result.motion_blur_return == f.caller && result.inverse == f.inverse,
          "call relationships resolve to fixture targets");
    Check(result.vertical_fov == f.fov && result.aspect == f.fov + 4 && result.inverse_view == f.inv,
          "fields are derived from their scalar and cache accesses");
}

void Rejected(const Fixture& f, const char* reason) {
    MotionBlurDiscovery result;
    Resolved(Fixture{}, result);
    std::string error;
    Check(!DiscoverMotionBlur(f.image.data(), f.image.size(), result, error), reason);
    Check(!error.empty(), "rejection provides a diagnostic");
    Check(result.player_camera_getter == 0 && result.view_rays == 0 && result.motion_blur_return == 0
          && result.inverse == 0 && result.inverse_view == 0 && result.vertical_fov == 0 && result.aspect == 0,
          "rejection clears every previously resolved value");
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2) {
        std::ifstream file(argv[1], std::ios::binary);
        if (!file) return 2;
        std::vector<std::uint8_t> image((std::istreambuf_iterator<char>(file)), {});
        MotionBlurDiscovery result;
        std::string error;
        if (!DiscoverMotionBlur(image.data(), image.size(), result, error)) {
            std::printf("%s\n", error.c_str()); return 1;
        }
        std::printf("getter=%X rays=%X caller=%X inverse=%X inv_offset=%X fov=%X aspect=%X\n",
                    result.player_camera_getter, result.view_rays, result.motion_blur_return,
                    result.inverse, result.inverse_view, result.vertical_fov, result.aspect);
        return 0;
    }
    MotionBlurDiscovery result;
    Resolved(Fixture{}, result);
    Resolved(Fixture{0x2000, 0x380}, result);
    auto chained = Fixture{};
    const auto end = chained.functions[0][1];
    chained.functions[0][1] = chained.pass + 14;
    chained.Put<std::uint8_t>(0x9900, 0x21);
    for (unsigned i = 0; i < 3; ++i) chained.Put<std::uint32_t>(0x9904 + i * 4, chained.functions[0][i]);
    chained.functions.push_back({chained.pass + 14, end, 0x9900});
    chained.Finish();
    Resolved(chained, result);
    auto missing = Fixture{};
    missing.image[missing.name] = 'X';
    Rejected(missing, "missing shader anchor is rejected");
    auto duplicate = Fixture{};
    std::memcpy(duplicate.image.data() + duplicate.name + 128, "MotionBlur", 11);
    Rejected(duplicate, "duplicate shader anchor is rejected");
    auto ambiguous = Fixture{};
    ambiguous.EmitPass(0x1800); ambiguous.Finish();
    Rejected(ambiguous, "duplicate complete call graph is rejected");
    auto width = Fixture{};
    width.image[width.rays] = 0xF2;
    Rejected(width, "double-width FOV is rejected");
    auto getter_width = Fixture{};
    getter_width.image[getter_width.getter + 9] = 0x40;
    Rejected(getter_width, "32-bit pointer loads are rejected");
    auto owner = Fixture{};
    owner.image[owner.rays + 3] = 0x87;
    Rejected(owner, "FOV on the wrong object is rejected");
    auto overwritten = Fixture{};
    overwritten.cursor = overwritten.overwrite;
    overwritten.Bytes({0x48, 0x8B, 0x36});
    Rejected(overwritten, "camera identity overwritten by a memory load is rejected");
    Rejected(Fixture{0, 0x241}, "unaligned field is rejected");
    auto angle = Fixture{};
    angle.Put<float>(angle.half, 1.0f);
    Rejected(angle, "wrong angle conversion is rejected");
    auto arity = Fixture{};
    arity.Put<std::uint32_t>(arity.caller + 7, 3);
    Rejected(arity, "caller must consume four output records");
    auto truncated = Fixture{};
    truncated.image.resize(0x200);
    Rejected(truncated, "truncated image is rejected");
    auto outside = Fixture{};
    outside.Put<std::uint32_t>(0x9000 + 4, 0x7000);
    Rejected(outside, "function extent outside executable section is rejected");
    auto noncode = Fixture{};
    noncode.Put<std::uint32_t>(noncode.caller - 4, noncode.name - noncode.caller);
    Rejected(noncode, "ray target in data is rejected");
    auto instruction = Fixture{};
    instruction.functions[1][1] -= 2;
    instruction.Finish();
    Rejected(instruction, "truncated instruction is rejected");
    auto cycle = Fixture{};
    cycle.Put<std::uint8_t>(0x9800, 0x21);
    for (unsigned i = 0; i < 3; ++i) cycle.Put<std::uint32_t>(0x9804 + i * 4, cycle.functions[0][i]);
    Rejected(cycle, "cyclic unwind chain is rejected");
    Resolved(Fixture{}, result);
    std::string snapshot_error;
    Check(!sr_ht::builds::DiscoverMotionBlurInModule(GetModuleHandleW(nullptr), result, snapshot_error),
          "snapshot of a non-game module rejects without calling native candidates");
    Check(!snapshot_error.empty() && result.view_rays == 0 && result.player_camera_getter == 0,
          "module snapshot failure clears prior discovery");
    return sr_test::Summary("render_discovery");
}
