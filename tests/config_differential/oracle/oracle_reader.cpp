// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// v0.2.0's reader and startup code, the newest published build (tag fcb3f90).
//
// The reader is compiled from byte copies: src/config.cpp, src/config.h,
// src/config_sanitize.h and src/logging.h beside this file are v0.2.0's files
// (git show v0.2.0:src/<file>), which CMakeLists.txt pins by hash. They are
// included inside namespace sr_oracle, after every header they include, so the
// published sr_ht::Config and sr_ht::LoadConfig become sr_oracle::sr_ht's and
// cannot collide with the mod's own. Every cameraunlock-core source they
// include holds the same bytes at v0.2.0's pin (c2d7914) and at this repo's
// (CMakeLists.txt pins those too).
//
// The startup code is transcribed from v0.2.0:src/headtracking_mod.cpp, which
// hooks the game and cannot be compiled into a test:
//
//   lines 71-99     ApplyConfigToPipeline, recording what it handed on
//   line 236        the yaw mode at startup, world up
//   lines 265-280   Bindings and RegisterHotkeys, recording each AddHotkey
//   lines 333-334   the pipeline and the enabled flag at startup
//   line 392        the Fov handed to InstallCameraHook

#include "oracle_reader.h"

#include <windows.h>

#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/logging/file_log.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/protocol/port_utils.h"

namespace sr_oracle {
#include "src/config.cpp"
}  // namespace sr_oracle

namespace sr_oracle {

Published Read(const std::string& exe_dir) {
    sr_ht::Config config;
    sr_ht::LoadConfig(exe_dir, config);

    Published p;
    p.udp_port = config.udp_port;
    p.fov_degrees = config.fov_degrees;
    p.world_yaw = true;

    // ApplyConfigToPipeline. SetPositionSettings replaces the smoothing fields
    // of the settings it is handed with the session's pair, which the two calls
    // before it had just set to these same values.
    p.sensitivity.yaw = config.yaw_sensitivity;
    p.sensitivity.pitch = config.pitch_sensitivity;
    p.sensitivity.roll = config.roll_sensitivity;
    p.sensitivity.invert_yaw = config.invert_yaw;
    p.sensitivity.invert_pitch = config.invert_pitch;
    p.sensitivity.invert_roll = config.invert_roll;
    p.local_smoothing = config.local_smoothing;
    p.remote_smoothing = config.remote_smoothing;
    p.position = cameraunlock::PositionSettings::Symmetric(
        config.position_sensitivity_x,
        config.position_sensitivity_y,
        config.position_sensitivity_z,
        config.limit_x, config.limit_y, config.limit_z, config.limit_z_back,
        config.local_smoothing, config.remote_smoothing,
        config.invert_position_x, config.invert_position_y, config.invert_position_z);
    p.position.local_smoothing = p.local_smoothing;
    p.position.remote_smoothing = p.remote_smoothing;
    p.mode = config.position_enabled ? kRotationAndPosition : kRotationOnly;

    p.tracking_enabled = config.enable_on_startup;

    // RegisterHotkeys: each action's nav key NavGuarded, its chord key
    // ChordGuarded.
    const struct { int nav_key; int chord_key; Action action; } bindings[] = {
        { config.toggle_key,     config.chord_toggle_key,     kToggle },
        { config.cycle_mode_key, config.chord_cycle_mode_key, kCycleMode },
        { config.yaw_mode_key,   config.chord_yaw_mode_key,   kYawMode },
    };
    for (const auto& binding : bindings) {
        p.hotkeys.emplace_back(binding.action, binding.nav_key, 0u);
        p.hotkeys.emplace_back(binding.action, binding.chord_key, 3u);
    }
    return p;
}

void WriteFirstRunFile(const std::string& exe_dir) {
    sr_ht::WriteDefaultConfigIfMissing(exe_dir);
}

}  // namespace sr_oracle
