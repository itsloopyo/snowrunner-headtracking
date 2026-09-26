// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace sr_ht {

// The settings CameraUnlock.ini holds, at their defaults.
struct Config {
    // Held as the socket's own type so no value outside the port range reaches
    // UdpReceiver::Start.
    std::uint16_t udp_port = 4242;
    bool enable_on_startup = true;

    // The tracking mode at startup, the pair the mode hotkey saves.
    bool rotation_enabled = true;
    bool position_enabled = true;

    // Whether head yaw turns about world up rather than the camera's own up
    // axis. The yaw hotkey saves it.
    bool world_space_yaw = true;

    // Smoothing for a tracker on this machine and for one on another device.
    // Rotation and position both use the pair.
    float local_smoothing = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // Travel limits in metres.
    float position_limit_x = cameraunlock::PositionSettings{}.limit_x;
    float position_limit_y = cameraunlock::PositionSettings{}.limit_y;
    float position_limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float position_limit_z = cameraunlock::PositionSettings{}.limit_z;
    float position_limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    std::string toggle_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::ToggleKey>::kCanonicalDefault;
    std::string cycle_tracking_mode_key = cameraunlock::config::schema::ConceptTraits<
        cameraunlock::config::schema::Concept::CycleTrackingModeKey>::kCanonicalDefault;
    std::string yaw_mode_key =
        cameraunlock::config::schema::ConceptTraits<cameraunlock::config::schema::Concept::YawModeKey>::kCanonicalDefault;

    // The angle the frame spans across its width, in degrees, reaching well past
    // what SnowRunner's own Field of View settings alone will go to. The mod
    // widens or narrows the frustum the game built onto this angle every frame,
    // so it lands there in the cabin and in the chase view alike, whatever the
    // game's two settings are set to. 0 leaves the engine's projection untouched.
    float fov_degrees = 0.0f;
};

// [Camera] Fov: 0, or an angle from 30 to 140. Neither end reaches the 180
// degrees at which a perspective projection stops existing, and below 30 the
// frame is a keyhole nobody meant.
class FovCodec {
public:
    using Value = float;

    static constexpr float kMin = 30.0f;
    static constexpr float kMax = 140.0f;

    cameraunlock::config::CodecParseResult<float> Parse(std::string_view text) const;
    // Throws std::invalid_argument for a value Parse would not read back.
    std::string Render(float value) const;
    bool Equal(float a, float b) const { return angle_.Equal(a, b); }

private:
    cameraunlock::config::FloatCodec angle_{0.0f, kMax};
};

// CameraUnlock.ini, beside SnowRunner.exe, in cameraunlock-core's canonical
// config format. One ConfigOwner reads and writes it; nothing else in the mod
// touches it. HeadTracking.ini, the file every earlier build read, is imported
// once while CameraUnlock.ini is absent and is never written.
namespace config {

cameraunlock::config::ConfigTable<Config> Table();

cameraunlock::config::RenderHeader Header();

// HeadTracking.ini through the frozen reader in src/legacy_config/, mapped into
// Config.
cameraunlock::config::LegacyImport<Config> Import();

// The owner's options for CameraUnlock.ini in `folder`, with HeadTracking.ini
// beside it as the legacy file and Defaults.ini where `defaults` says.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(const std::filesystem::path& folder,
                                                              cameraunlock::config::DefaultsFile defaults);

// Reads, imports or creates CameraUnlock.ini in `folder`, logs what the owner
// reports, and returns the settings the session runs on. Call once, from the
// bootstrap thread, with the log open. `defaults` is DefaultsFile::PerUser() in
// the mod.
Config Load(const std::filesystem::path& folder, cameraunlock::config::DefaultsFile defaults);

// The tracking mode the settings start in. The table never gives both rows
// false.
cameraunlock::TrackingMode StartupTrackingMode(const Config& config);

// What the position processor runs on: the limits from the file, the smoothing
// pair, and the pose as the tracker sends it.
cameraunlock::PositionSettings ToPositionSettings(const Config& config);

// Save the mode the cycle hotkey, and the yaw mode the yaw hotkey, has just
// applied. The session keeps it whether or not the save succeeds; a failed save
// is logged. Called on the hotkey thread.
void SaveTrackingMode(cameraunlock::TrackingMode mode);
void SaveWorldSpaceYaw(bool world_space_yaw);

}  // namespace config

}  // namespace sr_ht
