// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "session_vtable.h"

#include <windows.h>

#include <array>
#include <limits>

namespace sr_ht {

bool BoundSessionVtable(std::uintptr_t base, std::size_t size,
                       cameraunlock::memory::VtableInfo& table, std::string& error) {
    const auto candidate = table;
    table = {};
    error.clear();
    if (!base || size > std::numeric_limits<std::uintptr_t>::max() - base
        || candidate.vfunc_count <= 0
        || candidate.vfunc_count > cameraunlock::memory::kMaxVfuncEntries) {
        error = "invalid session vtable bounds";
        return false;
    }
    for (int slot = 0; slot < candidate.vfunc_count; ++slot) {
        const auto address = candidate.vfuncs[slot];
        MEMORY_BASIC_INFORMATION region{};
        if (address < base || address >= base + size
            || !VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))
            || region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD)) {
            error = "session vtable entry is outside readable image memory";
            return false;
        }
        if (region.Protect == PAGE_EXECUTE_READ || region.Protect == PAGE_EXECUTE_READWRITE
            || region.Protect == PAGE_EXECUTE_WRITECOPY) continue;

        // Core's walker bounds entries to the image, not its code sections. The
        // next table's RTTI locator is consequently included as another method.
        std::array<std::uint32_t, 6> locator{};
        SIZE_T read = 0;
        if (!slot || address % 4 || sizeof(locator) > base + size - address
            || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
                                  locator.data(), sizeof(locator), &read)
            || read != sizeof(locator) || locator[0] != 1 || locator[2] != 0
            || locator[5] != address - base || locator[3] >= size || locator[4] >= size) {
            error = "session vtable does not end at a valid RTTI locator";
            return false;
        }
        table = candidate;
        table.vfunc_count = slot;
        for (int i = slot; i < cameraunlock::memory::kMaxVfuncEntries; ++i) table.vfuncs[i] = 0;
        return true;
    }
    error = "session vtable boundary exceeds the inspected entries";
    return false;
}

}
