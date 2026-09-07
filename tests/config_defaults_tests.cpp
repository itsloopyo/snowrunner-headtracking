// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The defaults a user gets exist in three places: the Config struct's member
// initialisers (what the mod falls back to), the default HeadTracking.ini the
// mod writes on first run, and the reference HeadTracking.ini committed at the
// repo root. Nothing forces them to agree, and a drift between them is silent -
// the mod would behave one way and the documented file would say another.
//
// This locks all three together by round-tripping the real generator and the
// real loader.

#include "config.h"

#include "ini_fixture.h"
#include "test_support.h"

#include <windows.h>

#include <cstdio>
#include <string>

using namespace sr_ht;
using sr_test::Check;
using sr_test::CheckClose;

namespace {

// Every field of Config, compared against a default-constructed one. Loading a
// file that says exactly what the defaults say must leave the struct untouched.
void CheckMatchesDefaults(const Config& cfg, const char* source) {
    const Config defaults;
    std::printf("%s\n", source);

    Check(cfg.udp_port == defaults.udp_port, "UdpPort");
    Check(cfg.enable_on_startup == defaults.enable_on_startup, "EnableOnStartup");

    Check(cfg.toggle_key == defaults.toggle_key, "ToggleKey");
    Check(cfg.cycle_mode_key == defaults.cycle_mode_key, "CycleModeKey");
    Check(cfg.chord_toggle_key == defaults.chord_toggle_key, "ChordToggleKey");
    Check(cfg.chord_cycle_mode_key == defaults.chord_cycle_mode_key, "ChordCycleModeKey");

    CheckClose(cfg.yaw_sensitivity, defaults.yaw_sensitivity, "YawSensitivity");
    CheckClose(cfg.pitch_sensitivity, defaults.pitch_sensitivity, "PitchSensitivity");
    CheckClose(cfg.roll_sensitivity, defaults.roll_sensitivity, "RollSensitivity");
    Check(cfg.invert_yaw == defaults.invert_yaw, "InvertYaw");
    Check(cfg.invert_pitch == defaults.invert_pitch, "InvertPitch");
    Check(cfg.invert_roll == defaults.invert_roll, "InvertRoll");
    CheckClose(cfg.local_smoothing, defaults.local_smoothing, "LocalSmoothing");
    CheckClose(cfg.remote_smoothing, defaults.remote_smoothing, "RemoteSmoothing");

    Check(cfg.position_enabled == defaults.position_enabled, "Position Enabled");
    CheckClose(cfg.position_sensitivity_x, defaults.position_sensitivity_x, "SensitivityX");
    CheckClose(cfg.position_sensitivity_y, defaults.position_sensitivity_y, "SensitivityY");
    CheckClose(cfg.position_sensitivity_z, defaults.position_sensitivity_z, "SensitivityZ");
    Check(cfg.invert_position_x == defaults.invert_position_x, "InvertX");
    Check(cfg.invert_position_y == defaults.invert_position_y, "InvertY");
    Check(cfg.invert_position_z == defaults.invert_position_z, "InvertZ");
    CheckClose(cfg.limit_x, defaults.limit_x, "LimitX");
    CheckClose(cfg.limit_y, defaults.limit_y, "LimitY");
    CheckClose(cfg.limit_z, defaults.limit_z, "LimitZ");
    CheckClose(cfg.limit_z_back, defaults.limit_z_back, "LimitZBack");
}

// A Config with EVERY field moved off its default, hotkeys included.
//
// This is what makes CheckMatchesDefaults a gate rather than a restatement. The
// loader leaves a field alone when its key is absent, so starting from a default
// Config would let a key vanish from the file and still compare equal to the
// default. Both suites below start here instead, so a missing key shows up as
// the poisoned value surviving.
//
// Every value must be one the boundary checks ACCEPT - a value that gets clamped
// on the way in would land back on something legal and could mask the very drift
// this is looking for. The hotkeys are four bindable codes, all distinct, so
// RefuseCollidingHotkeys has nothing to say about them.
Config Poisoned() {
    Config cfg;
    cfg.udp_port = 5555;
    cfg.enable_on_startup = false;

    cfg.toggle_key = 0x70;            // F1
    cfg.cycle_mode_key = 0x71;        // F2
    cfg.chord_toggle_key = 0x72;      // F3
    cfg.chord_cycle_mode_key = 0x73;  // F4

    cfg.yaw_sensitivity = cfg.pitch_sensitivity = cfg.roll_sensitivity = 9.0f;
    cfg.invert_yaw = cfg.invert_pitch = cfg.invert_roll = true;
    cfg.local_smoothing = 0.99f;
    cfg.remote_smoothing = 0.99f;

    cfg.position_enabled = false;
    cfg.position_sensitivity_x = cfg.position_sensitivity_y = cfg.position_sensitivity_z = 9.0f;
    cfg.invert_position_x = cfg.invert_position_y = cfg.invert_position_z = true;
    cfg.limit_x = cfg.limit_y = cfg.limit_z = cfg.limit_z_back = 0.45f;
    return cfg;
}

// Reads a whole file as bytes. Empty on failure.
std::string ReadWholeFile(const std::string& path) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || f == nullptr) return {};
    std::string out;
    char buffer[4096];
    std::size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, got);
    std::fclose(f);
    return out;
}

// The two files have to be the same TEXT, not merely load to the same values.
// Comparing loaded values leaves the comments free to drift, and the comments
// are the whole of what the reference copy is for: the README quotes it, and a
// user comparing their file against it has nothing else to go on.
void CheckReferenceIniIsByteIdentical(const std::string& generated_path) {
    std::printf("The reference HeadTracking.ini is the file the mod writes\n");
    const std::string generated = ReadWholeFile(generated_path);
    const std::string reference = ReadWholeFile(std::string(SR_SOURCE_DIR) + "\\HeadTracking.ini");

    Check(!generated.empty(), "the generated file could be read back");
    Check(!reference.empty(), "the reference file at the repo root could be read");
    if (generated.empty() || reference.empty()) return;

    if (generated == reference) {
        std::printf("  ok:   the repo-root copy is byte-identical to the generated one\n");
        return;
    }
    std::printf("  FAIL: the repo-root HeadTracking.ini differs from the file the mod writes "
                "(%zu bytes vs %zu). Regenerate it from kDefaultIniText.\n",
                reference.size(), generated.size());
    ++sr_test::g_failures;
}

void GeneratedDefaultsTests() {
    const std::string dir = sr_test::MakeTempDir("config");
    if (dir.empty()) return;

    WriteDefaultConfigIfMissing(dir);

    const std::string path = sr_test::IniPathIn(dir);
    std::printf("The generated default HeadTracking.ini\n");
    Check(GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES,
          "is written when none exists");

    Config cfg = Poisoned();
    LoadConfig(dir, cfg);
    CheckMatchesDefaults(cfg, "The generated file loads back as the built-in defaults");

    CheckReferenceIniIsByteIdentical(path);

    // A second call must not clobber a file the user has since edited.
    FILE* f = nullptr;
    fopen_s(&f, path.c_str(), "w");
    Check(f != nullptr, "the test can rewrite the file");
    if (f) {
        std::fputs("[Network]\nUdpPort=5000\n", f);
        std::fclose(f);
    }
    WriteDefaultConfigIfMissing(dir);
    Config edited;
    LoadConfig(dir, edited);
    Check(edited.udp_port == 5000, "an existing HeadTracking.ini is never overwritten");

    sr_test::RemoveTempDir(dir);
}

void ReferenceIniTests() {
    // SR_SOURCE_DIR is the repo root, where the reference HeadTracking.ini
    // that ships as documentation lives. Poisoned in every field, so a key
    // MISSING from that file fails here instead of comparing equal to the
    // default it was never read into.
    Config cfg = Poisoned();
    LoadConfig(SR_SOURCE_DIR, cfg);
    CheckMatchesDefaults(cfg, "The reference HeadTracking.ini at the repo root");
}

}  // namespace

int main() {
    std::printf("SnowRunner head tracking - config default tests\n");
    std::printf("=====================================================\n");
    GeneratedDefaultsTests();
    ReferenceIniTests();
    return sr_test::Summary("config defaults");
}
