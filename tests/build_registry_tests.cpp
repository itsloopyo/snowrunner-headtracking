// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// Include the implementation to inspect dormant state without adding a runtime API.
#include "builds/build_registry.cpp"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace sr_ht::builds;
using sr_test::Check;

int main(int argc, char** argv) {
    if (argc == 2) {
        std::ifstream stream(argv[1], std::ios::binary);
        std::vector<std::uint8_t> mapped{std::istreambuf_iterator<char>(stream), {}};
        if (!Check(mapped.size() >= 0x1000, "read mapped installed image"))
            return sr_test::Summary("build_registry");
        std::uint32_t pe = 0;
        std::memcpy(&pe, mapped.data() + 0x3C, 4);
        if (!Check(pe < mapped.size() - 0x100, "mapped PE header is bounded"))
            return sr_test::Summary("build_registry");
        Check(SelectProfile(mapped.data()) == ProfileSelection::Matched,
              "installed image selects historical store profile");
        mapped[pe + 24 + 64] ^= 0x80;
        Check(SelectProfile(mapped.data()) == ProfileSelection::NoMatch && g_active == nullptr,
              "real selection rejects unlisted copy of installed image and clears prior selection");
        return sr_test::Summary("build_registry");
    }
    std::array<std::uint8_t, 512> image{};
    const std::uint16_t mz = 0x5A4D;
    const std::uint32_t nt_offset = 0x80;
    const std::uint32_t signature = 0x4550;
    std::memcpy(image.data(), &mz, sizeof(mz));
    std::memcpy(image.data() + 0x3C, &nt_offset, sizeof(nt_offset));
    std::memcpy(image.data() + nt_offset, &signature, sizeof(signature));

    for (const BuildProfile* profile : kKnownProfiles) {
        const auto select_known = [&] {
            std::memcpy(image.data() + 0x88, &profile->Fingerprint.TimeDateStamp, 4);
            std::memcpy(image.data() + 0xD0, &profile->Fingerprint.SizeOfImage, 4);
            std::memcpy(image.data() + 0xD8, &profile->Fingerprint.CheckSum, 4);
            Check(SelectProfile(image.data()) == ProfileSelection::Matched,
                  "historical fingerprint selects its profile");
            Check(&ActiveProfile() == profile, "the correct store profile is active");
        };

        for (const std::size_t offset : {0x88u, 0xD0u, 0xD8u}) {
            select_known();
            image[offset] ^= 0x80;
            Check(SelectProfile(image.data()) == ProfileSelection::NoMatch,
                  "an unlisted fingerprint cannot reuse historical offsets");
            Check(g_active == nullptr, "rejection clears the previously active profile");
        }

        select_known();
        image[0] = 0;
        Check(SelectProfile(image.data()) == ProfileSelection::NoMatch,
              "malformed headers are rejected");
        Check(g_active == nullptr, "malformed headers leave no active profile");
        std::memcpy(image.data(), &mz, sizeof(mz));

        select_known();
        Check(SelectProfile(nullptr) == ProfileSelection::NoMatch, "null image is rejected");
        Check(g_active == nullptr, "null image leaves no active profile");
    }
    return sr_test::Summary("build_registry");
}
