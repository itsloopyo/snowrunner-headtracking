// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "cameraunlock/memory/rtti_vtable.h"

namespace sr_ht {

bool BoundSessionVtable(std::uintptr_t base, std::size_t size,
                       cameraunlock::memory::VtableInfo& table, std::string& error);

}
