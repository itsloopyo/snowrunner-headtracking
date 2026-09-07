// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// The nav cluster is prime real estate on a sim rig - a button box, a wheel
// plugin or the game's own binds may already be sitting on Home or Page Up - so
// both halves of every action are remappable. What matters at this boundary is
// that a value the mod cannot bind leaves the action on the key it already had
// rather than on nothing, which is the difference between one hotkey not moving
// and a hotkey silently disappearing.

#include "config.h"

#include "ini_fixture.h"
#include "test_support.h"

#include <cstdio>
#include <string>

using namespace sr_ht;
using sr_test::Check;

namespace {

std::string g_dir;

// Loads `body` as the whole INI over a Config seeded with the shipped defaults,
// so every check reads as "what did this file change".
Config Load(const char* body) {
    if (!sr_test::WriteIni(g_dir, body)) return Config{};

    Config cfg;
    LoadConfig(g_dir, cfg);
    return cfg;
}

void RemapTests() {
    std::printf("Remapping a hotkey\n");

    const Config both = Load("[Hotkeys]\nToggleKey=0x2D\nChordToggleKey=0x4B\n");
    Check(both.toggle_key == 0x2D && both.chord_toggle_key == 0x4B,
          "moves both halves of an action");
    Check(both.cycle_mode_key == Config{}.cycle_mode_key,
          "and leaves the actions the file did not name alone");

    const Config bare = Load("[Hotkeys]\nToggleKey=2E\n");
    Check(bare.toggle_key == 0x2E, "reads a code with no 0x prefix as hex");

    const Config cycle = Load("[Hotkeys]\nCycleModeKey=0x70\nChordCycleModeKey=0x4A\n");
    Check(cycle.cycle_mode_key == 0x70 && cycle.chord_cycle_mode_key == 0x4A,
          "reaches the cycle-mode action");
}

void RefusedValuesKeepThePreviousBindingTests() {
    std::printf("A value the mod cannot bind\n");
    const Config defaults;

    // Ctrl is what the chord guard tests, so an action bound to it would either
    // never fire or fire on every chord press.
    Check(Load("[Hotkeys]\nToggleKey=0x11\n").toggle_key == defaults.toggle_key,
          "a modifier leaves the action on its previous key");

    Check(Load("[Hotkeys]\nCycleModeKey=0\n").cycle_mode_key == defaults.cycle_mode_key,
          "0 is not a key, and leaves the action on its previous key");

    Check(Load("[Hotkeys]\nCycleModeKey=0x220\n").cycle_mode_key == defaults.cycle_mode_key,
          "a code past 0xFE leaves the action on its previous key");

    // A name is what a user reaches for first, and half of them are made of hex
    // digits: read as a code "End" is 0xE and "Delete" is 0xDE, both bindable
    // and neither the key that was asked for.
    Check(Load("[Hotkeys]\nChordToggleKey=Insert\n").chord_toggle_key
              == defaults.chord_toggle_key,
          "a key name is refused rather than read as a code");
    Check(Load("[Hotkeys]\nToggleKey=Delete\n").toggle_key == defaults.toggle_key,
          "and a name that is all hex digits is refused too");

    Check(Load("[Hotkeys]\nCycleModeKey=0x2D ; Insert\n").cycle_mode_key == 0x2D,
          "a trailing comment is not junk, and the code is still read");

    Check(Load("[Hotkeys]\nToggleKey=\n").toggle_key == defaults.toggle_key,
          "an empty value leaves the action on its previous key");
}

void CollidingBindingsAreRefusedTests() {
    // HotkeyPoller keeps a list, not a map, so two actions on one code register
    // two entries and both callbacks run on a single press - tracking toggles
    // off AND the mode cycles, from one keystroke, with the log reporting the
    // key twice as if that were the intent.
    std::printf("Two actions cannot share one key\n");
    const Config defaults;

    const Config nav = Load("[Hotkeys]\nCycleModeKey=0x23\n");
    Check(nav.toggle_key == defaults.toggle_key && nav.cycle_mode_key == defaults.cycle_mode_key,
          "a nav key that collides with another action puts both bindings back");

    const Config navOther = Load("[Hotkeys]\nToggleKey=0x21\n");
    Check(navOther.toggle_key == defaults.toggle_key
              && navOther.cycle_mode_key == defaults.cycle_mode_key,
          "and it does not matter which of the pair the file moved");

    const Config chord = Load("[Hotkeys]\nChordCycleModeKey=0x59\n");
    Check(chord.chord_toggle_key == defaults.chord_toggle_key
              && chord.chord_cycle_mode_key == defaults.chord_cycle_mode_key,
          "the same holds for the chord half");

    // Both moved together to a pair that does not collide is a legitimate
    // remap, and refusing it would be the detector crying wolf.
    const Config moved = Load("[Hotkeys]\nToggleKey=0x24\nCycleModeKey=0x2D\n");
    Check(moved.toggle_key == 0x24 && moved.cycle_mode_key == 0x2D,
          "a pair moved to two distinct keys is taken");

    // Guard classes are separate: the nav binding only fires with the chord
    // released and the chord binding only while it is held, so one code in both
    // is unambiguous.
    const Config across = Load("[Hotkeys]\nToggleKey=0x47\n");
    Check(across.toggle_key == 0x47 && across.chord_cycle_mode_key == defaults.chord_cycle_mode_key,
          "a nav key may share a code with a chord letter");

    // The two halves of ONE action sharing a code still runs it once, whichever
    // guard passes.
    const Config sameAction = Load("[Hotkeys]\nChordToggleKey=0x23\n");
    Check(sameAction.toggle_key == 0x23 && sameAction.chord_toggle_key == 0x23,
          "and both halves of a single action may share one");
}

}  // namespace

int main() {
    std::printf("SnowRunner head tracking - hotkey remap tests\n");
    std::printf("===================================================\n");
    g_dir = sr_test::MakeTempDir("hotkey");
    if (g_dir.empty()) return sr_test::Summary("hotkey remap");
    RemapTests();
    RefusedValuesKeepThePreviousBindingTests();
    CollidingBindingsAreRefusedTests();
    sr_test::RemoveTempDir(g_dir);
    return sr_test::Summary("hotkey remap");
}
