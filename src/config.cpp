// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <windows.h>

#include "legacy_config/legacy_config.h"
#include "logging.h"

namespace sr_ht {

namespace {

constexpr char kIniName[] = "HeadTracking.ini";

// The file a fresh install lands with. Values here must stay in step with the
// Config struct's member initialisers - the config_defaults test locks that by
// generating this file and loading it back over a poisoned Config.
constexpr char kDefaultIniText[] =
    "; SnowRunner Head Tracking - configuration\r\n"
    "; Edit values, restart the game to apply.\r\n"
    ";\r\n"
    "; Controls (all remappable, see [Hotkeys]):\r\n"
    ";           End  / Ctrl+Shift+Y   toggle tracking\r\n"
    ";           PgUp / Ctrl+Shift+G   cycle tracking mode (rotation and position\r\n"
    ";                                 / rotation only / position only)\r\n"
    ";           PgDn / Ctrl+Shift+H   yaw about world up, or the camera's own\r\n\r\n"
    "[Network]\r\n"
    "UdpPort=4242\r\n\r\n"
    "[General]\r\n"
    "EnableOnStartup=1\r\n\r\n"
    "[Camera]\r\n"
    "; Actual horizontal field of view in degrees. 30 to 140.\r\n"
    "; Fov=130 spans 130 degrees across the screen in cabin and chase views.\r\n"
    "; SnowRunner's sliders use a different scale; matching numbers give different views.\r\n"
    "; The vertical angle follows the screen's aspect ratio.\r\n"
    "; 0 keeps the game's own Field of View settings.\r\n"
    "Fov=0\r\n\r\n"
    "[Hotkeys]\r\n"
    "; Windows virtual key codes, in hex. Each action has a nav-cluster key and a\r\n"
    "; Ctrl+Shift+<key> chord, and both fire it - remap either or both.\r\n"
    "; Common codes: End 0x23, Insert 0x2D, Delete 0x2E, PgUp 0x21,\r\n"
    "; PgDn 0x22, F1-F12 0x70-0x7B, A-Z 0x41-0x5A, numpad 0-9 0x60-0x69.\r\n"
    "ToggleKey=0x23\r\n"
    "CycleModeKey=0x21\r\n"
    "YawModeKey=0x22\r\n"
    "ChordToggleKey=0x59\r\n"
    "ChordCycleModeKey=0x47\r\n"
    "ChordYawModeKey=0x48\r\n\r\n"
    "[Rotation]\r\n"
    "YawSensitivity=1.0\r\n"
    "PitchSensitivity=1.0\r\n"
    "RollSensitivity=1.0\r\n"
    "InvertYaw=0\r\n"
    "InvertPitch=0\r\n"
    "InvertRoll=0\r\n"
    "; Smoothing covers rotation and position alike, and the value used is picked\r\n"
    "; per connection from where the tracker sends from. 0.0 none .. 1.0 heavy.\r\n"
    "LocalSmoothing=0.0\r\n"
    "RemoteSmoothing=0.15\r\n\r\n"
    "[Position]\r\n"
    "Enabled=1\r\n"
    "SensitivityX=1.0\r\n"
    "SensitivityY=1.0\r\n"
    "SensitivityZ=1.0\r\n"
    "InvertX=0\r\n"
    "InvertY=0\r\n"
    "InvertZ=0\r\n"
    "LimitX=0.30\r\n"
    "LimitY=0.20\r\n"
    "LimitZ=0.40\r\n"
    "LimitZBack=0.10\r\n";

std::string IniPath(const std::string& exe_dir) {
    return exe_dir + "\\" + kIniName;
}

}  // namespace

Config LoadConfig(const std::string& exe_dir) {
    legacy::Config read;
    legacy::LoadConfig(IniPath(exe_dir), read);

    Config out;
    out.udp_port = read.udp_port;
    out.enable_on_startup = read.enable_on_startup;
    out.fov_degrees = read.fov_degrees;
    out.toggle_key = read.toggle_key;
    out.cycle_mode_key = read.cycle_mode_key;
    out.yaw_mode_key = read.yaw_mode_key;
    out.chord_toggle_key = read.chord_toggle_key;
    out.chord_cycle_mode_key = read.chord_cycle_mode_key;
    out.chord_yaw_mode_key = read.chord_yaw_mode_key;
    out.yaw_sensitivity = read.yaw_sensitivity;
    out.pitch_sensitivity = read.pitch_sensitivity;
    out.roll_sensitivity = read.roll_sensitivity;
    out.invert_yaw = read.invert_yaw;
    out.invert_pitch = read.invert_pitch;
    out.invert_roll = read.invert_roll;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.position_enabled = read.position_enabled;
    out.position_sensitivity_x = read.position_sensitivity_x;
    out.position_sensitivity_y = read.position_sensitivity_y;
    out.position_sensitivity_z = read.position_sensitivity_z;
    out.invert_position_x = read.invert_position_x;
    out.invert_position_y = read.invert_position_y;
    out.invert_position_z = read.invert_position_z;
    out.limit_x = read.limit_x;
    out.limit_y = read.limit_y;
    out.limit_z = read.limit_z;
    out.limit_z_back = read.limit_z_back;
    return out;
}

void WriteDefaultConfigIfMissing(const std::string& exe_dir) {
    const std::string path = IniPath(exe_dir);

    // CREATE_NEW rather than "does it exist?" followed by a truncating open: the
    // two steps can straddle a file the user (or a second launch) writes in
    // between, and never overwriting a user's config is the whole promise here.
    const HANDLE file = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_EXISTS) return;
        Log::Line("[config] could not create %s (%lu) - the game directory is not writable. "
                  "Built-in defaults are in use and edits there will not be read.",
                  path.c_str(), error);
        return;
    }

    // A short write leaves a file that parses as a config but is missing keys,
    // which then reads as "the mod ignores my setting". Say so instead.
    constexpr DWORD kTextBytes = static_cast<DWORD>(sizeof(kDefaultIniText) - 1);
    DWORD written = 0;
    const BOOL ok = WriteFile(file, kDefaultIniText, kTextBytes, &written, nullptr);
    // Only meaningful when WriteFile actually failed. Reading it unconditionally
    // meant a short write that returned TRUE reported whatever error some
    // earlier, unrelated call had left in the thread.
    const DWORD writeError = ok ? 0 : GetLastError();
    CloseHandle(file);
    if (!ok || written != kTextBytes) {
        Log::Line("[config] %s was created but only %lu of %lu bytes could be written (%lu); "
                  "delete it and restart the game for a complete default config.",
                  path.c_str(), written, kTextBytes, writeError);
    }
}

}  // namespace sr_ht
