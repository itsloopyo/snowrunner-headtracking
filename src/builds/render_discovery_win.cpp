// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/render_discovery.h"

#include <windows.h>
#include <psapi.h>

#include <vector>

#include "cameraunlock/memory/pattern_scanner.h"

namespace sr_ht::builds {

bool DiscoverMotionBlurInModule(void* module, MotionBlurDiscovery& result, std::string& error) {
    result = {};
    MODULEINFO info{};
    if (!GetModuleInformation(GetCurrentProcess(), static_cast<HMODULE>(module), &info, sizeof(info))) {
        error = "GetModuleInformation failed: " + std::to_string(GetLastError());
        return false;
    }
    std::vector<std::uint8_t> image(info.SizeOfImage);
    const auto base = reinterpret_cast<std::uintptr_t>(info.lpBaseOfDll);
    auto cursor = base;
    std::uintptr_t readable = 0;
    std::size_t length = 0;
    while (cameraunlock::memory::NextReadableRange(cursor, base + info.SizeOfImage, readable, length)) {
        SIZE_T copied = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(readable),
                               image.data() + readable - base, length, &copied) || copied != length) {
            error = "reading mapped image failed: " + std::to_string(GetLastError());
            return false;
        }
    }
    return DiscoverMotionBlur(image.data(), image.size(), result, error);
}

}  // namespace sr_ht::builds
