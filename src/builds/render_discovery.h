// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sr_ht::builds {

struct MotionBlurDiscovery {
    std::uint32_t player_camera_getter = 0;
    std::uint32_t view_rays = 0;
    std::uint32_t motion_blur_return = 0;
    std::uint32_t inverse = 0;
    std::uint32_t inverse_view = 0;
    std::uint32_t vertical_fov = 0;
    std::uint32_t aspect = 0;
};

bool DiscoverMotionBlur(const std::uint8_t* image, std::size_t size,
                        MotionBlurDiscovery& result, std::string& error);
bool DiscoverMotionBlurInModule(void* module, MotionBlurDiscovery& result, std::string& error);

}  // namespace sr_ht::builds
