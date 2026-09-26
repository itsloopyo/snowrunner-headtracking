// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// CameraUnlock.ini on the canonical format: the committed file (HeadTracking.ini
// in the repo, the path core's data/config-format.json records) is the table's
// fresh render, a first launch creates exactly those bytes, the mode cycle's
// and the yaw toggle's saves change the lines of their rows and no other byte,
// End's row is not saved, a row set to default follows Defaults.ini, [Camera]
// Fov reads 0 or 30 to 140 and nothing else, and the default hotkeys are the
// fleet's.
//
// sr_config_tests --render-config <path> writes the rendered file to <path>
// instead (pixi run render-config).

#include "config.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include "cameraunlock/config/canonical_ini.h"
#include "cameraunlock/input/key_bindings.h"

namespace fs = std::filesystem;
namespace cfg = cameraunlock::config;
namespace config = sr_ht::config;

using sr_ht::Config;
using sr_ht::FovCodec;
using cameraunlock::TrackingMode;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& message) {
    if (condition) return;
    std::printf("FAIL: %s\n", message.c_str());
    ++g_failures;
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

std::string Replace(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::logic_error("'" + from + "' is not in the text");
    return text.replace(at, from.size(), to);
}

std::string Rendered() { return cfg::RenderCanonicalFresh(config::Table(), config::Header()); }

fs::path TempDir() {
    wchar_t temp[MAX_PATH + 1] = {};
    if (GetTempPathW(MAX_PATH + 1, temp) == 0) throw std::runtime_error("GetTempPathW failed");
    const fs::path dir = fs::path(temp) / ("sr_ht_config_tests_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void RenderMatchesCommittedFile() {
    const std::string committed = ReadBytes(fs::path(SR_SOURCE_DIR) / "HeadTracking.ini");
    Check(committed == Rendered(),
          "HeadTracking.ini, the committed CameraUnlock.ini, is not the table's fresh render; run pixi run render-config");
    const cfg::CanonicalIni doc = cfg::ParseCanonicalIni(committed);
    Check(doc.IsReadable() && doc.diagnostics.empty(), "the committed file reads without a diagnostic");
    Config read = config::Table().defaults();
    Check(cfg::ApplyCanonical(doc, config::Table(), read).diagnostics.empty(),
          "the committed file applies without a diagnostic");
}

void DefaultsAreTheFleetDefaults() {
    using cameraunlock::input::KeyBinding;
    using cameraunlock::input::KeyModifiers;
    constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;
    const Config defaults = config::Table().defaults();
    Check(defaults.toggle_key == "End, Ctrl+Shift+Y", "ToggleKey defaults to End, Ctrl+Shift+Y");
    Check(defaults.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G",
          "CycleTrackingModeKey defaults to PageUp, Ctrl+Shift+G");
    Check(defaults.yaw_mode_key == "PageDown, Ctrl+Shift+H", "YawModeKey defaults to PageDown, Ctrl+Shift+H");
    Check(defaults.udp_port == 4242, "the port defaults to 4242");
    Check(defaults.enable_on_startup, "head tracking is on at startup by default");
    Check(defaults.world_space_yaw, "head yaw turns about world up by default");
    Check(defaults.fov_degrees == 0.0f, "Fov defaults to 0, the game's own field of view");
    Check(config::StartupTrackingMode(defaults) == TrackingMode::RotationAndPosition,
          "the default tracking mode is rotation and position");
    const struct { const std::string* list; int vk; char chord; const char* what; } lists[] = {
        {&defaults.toggle_key, VK_END, 'Y', "ToggleKey registers End and Ctrl+Shift+Y"},
        {&defaults.cycle_tracking_mode_key, VK_PRIOR, 'G', "CycleTrackingModeKey registers Page Up and Ctrl+Shift+G"},
        {&defaults.yaw_mode_key, VK_NEXT, 'H', "YawModeKey registers Page Down and Ctrl+Shift+H"},
    };
    for (const auto& l : lists) {
        const auto parsed = cameraunlock::input::ParseKeyBindings(*l.list);
        Check(parsed.ok() && parsed.bindings.size() == 2 &&
                  parsed.bindings[0] == KeyBinding{KeyModifiers::kNone, l.vk} &&
                  parsed.bindings[1] == KeyBinding{kChord, l.chord},
              l.what);
    }

    const cameraunlock::PositionSettings position = config::ToPositionSettings(defaults);
    const cameraunlock::PositionSettings core;
    Check(position.sensitivity_x == 1.0f && position.sensitivity_y == 1.0f && position.sensitivity_z == 1.0f &&
              !position.invert_x && !position.invert_y && !position.invert_z,
          "the position processor applies the lean as the tracker sends it");
    Check(position.limit_x == core.limit_x && position.limit_y == core.limit_y &&
              position.limit_y_down == core.limit_y_down && position.limit_z == core.limit_z &&
              position.limit_z_back == core.limit_z_back,
          "the default limits are core's");
}

// The saves change the lines of their own rows, from default to the value, and
// no other byte; End's row is one no save may change; the next launch reads the
// saved mode and yaw.
void SavesChangeOnlyTheirRows(const fs::path& dir) {
    const fs::path folder = dir / "saves";
    fs::create_directories(folder);
    const fs::path defaults = dir / "saves-global" / "Defaults.ini";
    const fs::path path = folder / "CameraUnlock.ini";
    const auto options = [&] { return config::OwnerOptions(folder, cfg::DefaultsFile::At(defaults.wstring())); };

    cfg::ConfigOwner<Config> owner(options());
    const cfg::ConfigLoadResult<Config> created = owner.Load();
    Check(created.status == cfg::ConfigLoadStatus::Created,
          std::string("a first launch with no legacy file creates the file, not ") +
              cfg::ConfigLoadStatusName(created.status));
    const std::string fresh = ReadBytes(path);
    Check(fresh == Rendered(), "a first launch writes the committed file's bytes");
    Check(!fs::exists(folder / "HeadTracking.ini"), "a first launch writes no legacy file");
    const std::string defaultsBytes = ReadBytes(defaults);

    const cameraunlock::TrackingModeChannels positionOnly = cameraunlock::EncodeTrackingMode(TrackingMode::PositionOnly);
    Check(owner.Save([&](Config& c) {
                  c.rotation_enabled = positionOnly.rotation_enabled;
                  c.position_enabled = positionOnly.position_enabled;
              }).status == cfg::ConfigSaveStatus::Saved,
          "the mode cycle saves");
    const std::string afterMode = ReadBytes(path);
    Check(afterMode == Replace(Replace(fresh, "\r\nRotationEnabled=default\r\n", "\r\nRotationEnabled=false\r\n"),
                               "\r\nPositionEnabled=default\r\n", "\r\nPositionEnabled=true\r\n"),
          "the mode cycle changes the two mode lines and no other byte");

    Check(owner.Save([](Config& c) { c.world_space_yaw = false; }).status == cfg::ConfigSaveStatus::Saved,
          "the yaw toggle saves");
    const std::string afterYaw = ReadBytes(path);
    Check(afterYaw == Replace(afterMode, "\r\nWorldSpaceYaw=default\r\n", "\r\nWorldSpaceYaw=false\r\n"),
          "the yaw toggle changes the WorldSpaceYaw line and no other byte");

    bool refused = false;
    try {
        owner.Save([](Config& c) { c.enable_on_startup = false; });
    } catch (const std::exception&) {
        refused = true;
    }
    Check(refused, "EnableOnStartup is not a row a save may change, so End can never persist");
    Check(ReadBytes(path) == afterYaw, "a refused save writes nothing");
    Check(ReadBytes(defaults) == defaultsBytes, "no save writes Defaults.ini");

    const cfg::ConfigLoadResult<Config> next = cfg::ConfigOwner<Config>(options()).Load();
    Check(next.status == cfg::ConfigLoadStatus::Canonical, "the next launch reads CameraUnlock.ini");
    Check(config::StartupTrackingMode(next.config) == TrackingMode::PositionOnly,
          "the next launch starts in position only");
    Check(!next.config.world_space_yaw, "the next launch starts with yaw about the camera's own up axis");
    Check(next.config.enable_on_startup, "End's row keeps its default");
    Check(ReadBytes(path) == afterYaw, "the next launch writes nothing");
}

// A row holding default takes Defaults.ini's value.
void DefaultRowsFollowDefaultsIni(const fs::path& dir) {
    const fs::path folder = dir / "follow";
    fs::create_directories(folder);
    const fs::path defaults = dir / "follow-global" / "Defaults.ini";
    fs::create_directories(defaults.parent_path());
    WriteBytes(defaults,
               "[CameraUnlock]\r\nConfigFormat=1\r\n\r\n[Network]\r\nUdpPort=5252\r\n\r\n"
               "[General]\r\nEnableOnStartup=false\r\nWorldSpaceYaw=false\r\n\r\n[Hotkeys]\r\nToggleKey=F8\r\n");
    WriteBytes(folder / "CameraUnlock.ini", Rendered());
    const cfg::ConfigLoadResult<Config> loaded =
        cfg::ConfigOwner<Config>(config::OwnerOptions(folder, cfg::DefaultsFile::At(defaults.wstring()))).Load();
    Check(loaded.status == cfg::ConfigLoadStatus::Canonical, "the committed file loads as canonical");
    Check(loaded.config.udp_port == 5252, "UdpPort=default follows Defaults.ini");
    Check(!loaded.config.enable_on_startup, "EnableOnStartup=default follows Defaults.ini");
    Check(!loaded.config.world_space_yaw, "WorldSpaceYaw=default follows Defaults.ini");
    Check(loaded.config.toggle_key == "F8", "ToggleKey=default follows Defaults.ini");
    Check(loaded.config.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G",
          "a row Defaults.ini does not set keeps the built-in value");
}

// [Camera] Fov holds 0 or an angle from 30 to 140; anything else keeps the
// default, 0, with a diagnostic, as every other refused value does.
void FovReadsOnlyItsRange() {
    const FovCodec codec;
    const struct { const char* text; bool ok; float value; } cases[] = {
        {"0", true, 0.0f},   {"30", true, 30.0f},   {"95.5", true, 95.5f}, {"140", true, 140.0f},
        {"10", false, 0.0f}, {"29.9", false, 0.0f}, {"141", false, 0.0f},  {"-5", false, 0.0f},
        {"nan", false, 0.0f}, {"wide", false, 0.0f},
    };
    for (const auto& c : cases) {
        const auto read = codec.Parse(c.text);
        Check(read.ok() == c.ok && (!c.ok || read.value == c.value),
              std::string("[Camera] Fov=") + c.text + (c.ok ? " reads" : " is refused"));
        if (c.ok) Check(codec.Parse(codec.Render(read.value)).value == read.value,
                        std::string("[Camera] Fov=") + c.text + " renders and reads back");
    }
    bool threw = false;
    try {
        codec.Render(10.0f);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    Check(threw, "a Fov between 0 and 30 is never written");

    const std::string file = Replace(Rendered(), "\r\nFov=0.0\r\n", "\r\nFov=15\r\n");
    Config read = config::Table().defaults();
    const auto applied = cfg::ApplyCanonical(cfg::ParseCanonicalIni(file), config::Table(), read);
    Check(applied.diagnostics.size() == 1 && read.fov_degrees == 0.0f,
          "Fov=15 in CameraUnlock.ini keeps the default and draws one diagnostic");
}

}  // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc == 3 && std::string(argv[1]) == "--render-config") {
        WriteBytes(argv[2], Rendered());
        std::printf("wrote %s\n", argv[2]);
        return 0;
    }
    if (argc != 1) {
        std::printf("usage: sr_config_tests [--render-config <path>]\n");
        return 2;
    }
    const fs::path dir = TempDir();
    RenderMatchesCommittedFile();
    DefaultsAreTheFleetDefaults();
    SavesChangeOnlyTheirRows(dir);
    DefaultRowsFollowDefaultsIni(dir);
    FovReadsOnlyItsRange();
    fs::remove_all(dir);
    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all config checks passed\n");
    return 0;
}
