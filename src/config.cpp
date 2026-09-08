// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <windows.h>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "config_sanitize.h"
#include "logging.h"

#include "cameraunlock/config/ini_reader.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/protocol/port_utils.h"

namespace sr_ht {

namespace {

constexpr char kIniName[] = "HeadTracking.ini";

// Named once so the loader, the diagnostics and the shipped default file cannot
// drift onto a section that does not exist: a mistyped section name reads every
// key in it as absent, which looks exactly like a user who never wrote them.
constexpr char kSectionNetwork[]  = "Network";
constexpr char kSectionGeneral[]  = "General";
constexpr char kSectionCamera[]   = "Camera";
constexpr char kSectionHotkeys[]  = "Hotkeys";
constexpr char kSectionRotation[] = "Rotation";
constexpr char kSectionPosition[] = "Position";

// The shipped default for each smoothing key, mirroring the Config member
// initialisers. They are named here because a refused value has to land on the
// default of the key it came from, and the two keys do not share one.
constexpr float kDefaultLocalSmoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
constexpr float kDefaultRemoteSmoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

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
    "; SnowRunner has its own Field of View settings, one for the cabin view and\r\n"
    "; one for the chase view, and FovScale multiplies whichever of them the game\r\n"
    "; is rendering with - so the two views keep the difference those settings\r\n"
    "; give them, and either can be taken past what the game's own setting\r\n"
    "; reaches. 1.25 puts a quarter more of the world across the frame, 0.8 shows\r\n"
    "; less. Range 0.5 to 2.0. 1.0 leaves the game's projection untouched.\r\n"
    ";\r\n"
    "; This is a rendering setting rather than head tracking, so it stays applied\r\n"
    "; while tracking is toggled off, and turning your head ten degrees turns the\r\n"
    "; view ten degrees at every setting. HeadTracking.log names the angles it\r\n"
    "; started from and the ones it produced.\r\n"
    "FovScale=1.0\r\n\r\n"
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

// A value the mod refused is exactly what a "my INI setting does nothing" bug
// report needs to show, so every substitution is logged rather than swallowed.
// A NaN raw value compares unequal to everything, including itself, so it
// takes this branch too.
float UseSanitized(const char* name, float raw, float clean) {
    if (raw != clean) {
        Log::Line("[config] %s=%.4f is out of range or not finite; using %.4f",
                  name, raw, clean);
    }
    return clean;
}

// Present but unparseable was the one INI failure with nothing in the log to
// show for it. Every reader in IniReader answers it with the fallback the caller
// passed, and each caller here passes the value the Config already holds, so the
// VALUE was already right - the key kept what it had. What was missing is any
// way to tell that from a key the user never wrote, which is the difference
// between a triageable "my setting is ignored" report and an untriageable one.
// Every other refusal in this file is logged; these were not.
//
// Two probes with opposite fallbacks separate the two cases without duplicating
// the parser: agree, and the reader parsed the text; differ, and it echoed each
// fallback straight back.
//
// The trap this exists for is a bool with a trailing comment. IniReader's own
// header documents it: GetPrivateProfileString does not treat ';' as a comment
// introducer, and ReadBool matches the WHOLE value, so `Enabled=0 ; no lean`
// matches nothing, position tracking stays on, and the user is told why.
bool ParsedBool(const cameraunlock::IniReader& ini, const char* section,
                const char* key, bool& out) {
    const bool as_true = ini.ReadBool(section, key, true);
    if (as_true != ini.ReadBool(section, key, false)) return false;
    out = as_true;
    return true;
}

// Parsed here rather than through IniReader::ReadFloat, because ReadFloat
// answers with whatever numeric PREFIX it finds and there is no way to ask it
// whether the rest of the value was junk. That is not hypothetical: a decimal
// comma is the norm across most of continental Europe, and `LimitX=0,25` came
// through as a clean 0.0 - X lean silently switched off, nothing in the log,
// because the parsed 0.0 then equalled its own sanitized value. `2,5` arrived
// as 2.0 and `2.5abc` as 2.5.
//
// So the rule ParseVirtualKey already applied to hotkeys applies here too: the
// WHOLE value has to be the number, bar trailing space and a comment. The C
// locale is pinned so a machine set to a comma decimal separator cannot change
// what the file means - a config is a file format, not a document.
//
// Inf and NaN still count as parsed and go on to the finite checks in
// config_sanitize.h, which is where a value out of the float range is answered.
bool ParseFloatText(const std::string& text, float& out) {
    const char* start = text.c_str();
    char* end = nullptr;
#ifdef _MSC_VER
    static const _locale_t c_numeric = _create_locale(LC_NUMERIC, "C");
    // _strtod_l falls back to the THREAD's locale on a null, which is exactly
    // what pinning "C" is here to prevent - a comma-decimal machine would
    // silently start reading the file by different rules. Refusing every float
    // instead is loud: the log names each key and says what it kept.
    if (c_numeric == nullptr) return false;
    const double value = _strtod_l(start, &end, c_numeric);
#else
    const double value = std::strtod(start, &end);
#endif
    if (end == start) return false;

    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0' && *end != ';' && *end != '#') return false;

    out = static_cast<float>(value);
    return true;
}

// The same rule for the one integer key, so UdpPort cannot be the one place a
// junk suffix or a stray prefix is quietly accepted while every other key
// refuses it.
bool ParseDecimalInt(const std::string& text, long& out) {
    const char* start = text.c_str();
    char* end = nullptr;
    const long value = std::strtol(start, &end, 10);
    if (end == start) return false;

    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0' && *end != ';' && *end != '#') return false;

    out = value;
    return true;
}

bool ReadFlag(const cameraunlock::IniReader& ini, const char* section,
              const char* key, bool current) {
    const std::string text = ini.ReadString(section, key, "");
    if (text.empty()) return current;

    bool parsed = false;
    if (ParsedBool(ini, section, key, parsed)) return parsed;

    // The trailing-comment sentence used to be unconditional, so `Enabled=TrUe`
    // sent the user hunting for a comment they had never written. It is the
    // right answer only when the value actually carries one; the rest of the
    // time the fault is the casing, because ReadBool matches exactly.
    const bool has_comment = text.find(';') != std::string::npos
                          || text.find('#') != std::string::npos;
    Log::Line("[config] %s=%s is not 0 or 1 (or true/false, yes/no, on/off, matched exactly); "
              "keeping %d.%s",
              key, text.c_str(), current ? 1 : 0,
              has_comment ? " A trailing ; comment is part of the value here - put comments on "
                            "their own line above the key." : "");
    return current;
}

// Shared by every float key: refuse text that will not parse, and hand what does
// parse to the caller's own boundary check. `current` is what the key keeps when
// the text is unreadable; where an out-of-range or non-finite number lands is
// `sanitize`'s business, because the two smoothing keys do not share a default.
template <typename Sanitize>
float ReadFloatValue(const cameraunlock::IniReader& ini, const char* section,
                     const char* key, float current, Sanitize sanitize) {
    const std::string text = ini.ReadString(section, key, "");
    if (text.empty()) return current;

    float raw = 0.0f;
    if (!ParseFloatText(text, raw)) {
        // The comma is named because it is the one fault a user cannot guess
        // from "is not a number" - the value looks like a number, and on their
        // machine, in every other application, it is one.
        Log::Line("[config] %s=%s is not a number; keeping %.4f.%s", key, text.c_str(), current,
                  text.find(',') != std::string::npos
                      ? " Use a full stop for the decimal point, not a comma." : "");
        return current;
    }
    return UseSanitized(key, raw, sanitize(raw));
}

float ReadSensitivity(const cameraunlock::IniReader& ini, const char* section,
                      const char* key, float current) {
    return ReadFloatValue(ini, section, key, current,
                          [](float raw) { return SanitizeSensitivity(raw); });
}

// `shipped_default` is the default of the key being read, not one shared by
// both smoothing keys: LocalSmoothing falls back to 0.0, RemoteSmoothing to
// 0.15. A single fallback would answer a malformed RemoteSmoothing with the
// LOCAL default, so a phone on WiFi would get no smoothing at all on raw
// network jitter, which is the one case RemoteSmoothing exists to cover.
float ReadSmoothing(const cameraunlock::IniReader& ini, const char* section,
                    const char* key, float current, float shipped_default) {
    return ReadFloatValue(ini, section, key, current, [shipped_default](float raw) {
        return SanitizeSmoothing(raw, shipped_default);
    });
}

// Warned once per process rather than once per load: config is reloadable, and
// repeating this on every reload buries it.
//
// The old value is deliberately NOT migrated into the new keys. The single
// Smoothing value carried a hidden 0.15 floor, so the number in an existing
// config does not mean what it used to: copying it across would hand a local
// user smoothing they never chose under the new semantics, and copying it into
// only one of the two keys would be a guess about which connection they were on.
void WarnRetiredSmoothingKey(const cameraunlock::IniReader& ini,
                             const char* section, const char* key) {
    static bool warned = false;
    if (warned) return;
    if (ini.ReadString(section, key, "").empty()) return;
    warned = true;
    Log::Line(
        "[config] key [%s] %s has been retired and is IGNORED. Smoothing is now two "
        "keys: LocalSmoothing (default 0, applies to a tracker on this machine) and "
        "RemoteSmoothing (default 0.15, applies to a tracker on the network). The "
        "old value is not migrated because the semantics changed - it carried a "
        "hidden 0.15 floor that no longer exists. Set the two new keys.",
        section, key);
}

// Virtual key codes are published as hex and that is how the shipped INI writes
// them, so that is how they are read: a bare 24 is 0x24, not 36. IniReader's
// ReadHex cannot tell an absent key from an unreadable one, and both matter
// here - the first is the common case, the second is a user who typed a key
// name and needs to be told it is codes only.
bool ParseVirtualKey(const std::string& text, int& out) {
    const char* start = text.c_str();
    if (text.size() > 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
        start += 2;
    }
    char* end = nullptr;
    const long value = std::strtol(start, &end, 16);
    if (end == start) return false;

    // The whole value has to be the code, not just the front of it. Half the
    // key names a user would try are made of hex digits - "End" reads as 0xE
    // and "Delete" as 0xDE, both perfectly bindable keys and neither the one
    // that was asked for. Only trailing space and a comment are allowed past
    // the number.
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end != '\0' && *end != ';' && *end != '#') return false;

    out = static_cast<int>(value);
    return true;
}

// A key the mod refused leaves that action on its previous binding rather than
// on nothing, so a mistyped code costs the user one hotkey and says so.
int ReadKey(const cameraunlock::IniReader& ini, const char* key, int fallback) {
    const std::string text = ini.ReadString(kSectionHotkeys, key, "");
    if (text.empty()) return fallback;

    int raw = 0;
    if (!ParseVirtualKey(text, raw)) {
        Log::Line("[config] %s=%s is not a virtual key code (0x23, or 23 read as hex); "
                  "keeping 0x%X", key, text.c_str(), fallback);
        return fallback;
    }
    if (!IsBindableVirtualKey(raw)) {
        Log::Line("[config] %s=%s is not a key that can be bound; keeping 0x%X",
                  key, text.c_str(), fallback);
        return fallback;
    }
    return raw;
}

float ReadLimit(const cameraunlock::IniReader& ini, const char* key, float current) {
    return ReadFloatValue(ini, kSectionPosition, key, current, [current](float raw) {
        return SanitizePositionLimit(raw, current);
    });
}

// The one key not read through ReadFloatValue's parse-then-sanitize path,
// because it is an integer with its own range check in core. It still has to
// name the right fault: ReadInt answers a present but unparseable value with 0,
// NormalizeUdpPort then refuses the 0, and `UdpPort=auto` came back as "outside
// 1024-65535" - sending the user to check a range that was never the problem.
// Reading the raw text first also makes an empty value mean unset here, as it
// already does for every other key.
void ReadUdpPort(const cameraunlock::IniReader& ini, Config& out) {
    const std::string text = ini.ReadString(kSectionNetwork, "UdpPort", "");
    if (text.empty()) return;

    long parsed = 0;
    if (!ParseDecimalInt(text, parsed)) {
        Log::Line("[config] UdpPort=%s is not a number; using %u", text.c_str(),
                  static_cast<unsigned>(out.udp_port));
        return;
    }

    bool valid = false;
    const std::uint16_t port = cameraunlock::NormalizeUdpPort(
        static_cast<int>(parsed), out.udp_port, valid);
    if (valid) {
        out.udp_port = port;
        return;
    }
    Log::Line("[config] UdpPort=%s is outside 1024-65535; using %u", text.c_str(),
              static_cast<unsigned>(out.udp_port));
}

// Two actions on one key run both on a single press. Nothing downstream can
// separate them: HotkeyPoller keeps a list rather than a map, so a duplicate
// code registers a second entry and both callbacks fire - the player toggles
// tracking off and cycles the mode with the same keystroke, and the log line
// that reports the bindings shows the key twice with no hint that it is a fault.
//
// Refused here, at the boundary, with both bindings put back to what they came
// in as. `previous` is the Config the loader was handed, which the bootstrap
// builds from the shipped defaults, so restoring it always resolves the
// collision rather than trading it for another one.
//
// Only within a guard class: ToggleKey and ChordCycleModeKey may share a code
// (one fires with Ctrl+Shift held, the other only without), and the two keys of
// a single action sharing one is harmless - it still runs once either way.
void RefuseCollidingHotkeys(const Config& previous, Config& out) {
    struct Binding { const char* name; int Config::* member; };
    static const Binding kNav[3] = {
        { "ToggleKey",    &Config::toggle_key },
        { "CycleModeKey", &Config::cycle_mode_key },
        { "YawModeKey",   &Config::yaw_mode_key },
    };
    static const Binding kChord[3] = {
        { "ChordToggleKey",    &Config::chord_toggle_key },
        { "ChordCycleModeKey", &Config::chord_cycle_mode_key },
        { "ChordYawModeKey",   &Config::chord_yaw_mode_key },
    };

    for (const Binding* group : { kNav, kChord }) {
        for (int a = 0; a < 3; ++a) {
            for (int b = a + 1; b < 3; ++b) {
                if (out.*group[a].member != out.*group[b].member) continue;
                Log::Line("[config] %s and %s are both 0x%X - one binding cannot run two "
                          "actions; keeping 0x%X and 0x%X",
                          group[a].name, group[b].name, out.*group[a].member,
                          previous.*group[a].member, previous.*group[b].member);
                out.*group[a].member = previous.*group[a].member;
                out.*group[b].member = previous.*group[b].member;
            }
        }
    }
}

// GetPrivateProfileString does not skip a UTF-8 byte order mark, so the first
// line of the file reads as three junk bytes followed by its real text.
//
// The shipped file opens with a comment, so a BOM lands on a line that is
// discarded anyway and costs nothing - measured, not assumed. A file that opens
// with a section header is the case that bites: `[Network]` becomes
// `<BOM>[Network]`, matches no section, and every key under it is read as
// absent while the sections below it parse normally. That is a config half
// applied with nothing in the log, so it gets a line.
void WarnUtf8Bom(const std::string& path) {
    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || file == nullptr) return;
    unsigned char head[3] = { 0, 0, 0 };
    const std::size_t got = std::fread(head, 1, sizeof(head), file);
    std::fclose(file);
    if (got != sizeof(head) || head[0] != 0xEF || head[1] != 0xBB || head[2] != 0xBF) return;

    Log::Line("[config] %s starts with a UTF-8 byte order mark. Windows' INI reader does not "
              "skip it, so the file's FIRST line is unreadable - harmless above a comment, but "
              "if a section header is first, every key in that section is ignored. Re-save the "
              "file as ANSI or as UTF-8 without a BOM.", path.c_str());
}

// A key the mod does not read is indistinguishable from one it read and
// ignored, and both look like "the mod ignores my setting". A misspelling
// (`YawSensitivty`), a key under the wrong heading (`RemoteSmoothing` filed
// under [Position], which the smoothing comment invites), and a leftover from
// another mod's config all land here.
//
// Boot only, and only over the sections this mod owns, so a file carrying
// another tool's sections is left alone.
void WarnUnknownKeys(const std::string& path, const char* section,
                     const char* const* known, std::size_t known_count) {
    std::vector<char> buffer(4096);
    const DWORD used = GetPrivateProfileSectionA(section, buffer.data(),
                                                 static_cast<DWORD>(buffer.size()), path.c_str());
    // GetPrivateProfileSectionA signals a full buffer with size-2 and gives no
    // way to ask for more, so an oversized section is left unchecked rather than
    // half checked - reporting the keys that happened to fit would be worse.
    if (used == 0 || used >= buffer.size() - 2) return;

    for (const char* entry = buffer.data(); *entry != '\0'; entry += std::strlen(entry) + 1) {
        // GetPrivateProfileSectionA drops ';' comment lines but hands back '#'
        // ones verbatim, and this mod treats '#' as a comment everywhere else -
        // so without this a commented-out `# LimitX=0.25` drew a confident
        // complaint about a key named "# LimitX".
        const char* text = entry;
        while (*text == ' ' || *text == '\t') ++text;
        if (*text == '#' || *text == ';') continue;

        const char* equals = std::strchr(text, '=');
        if (equals == nullptr) continue;

        std::string name(text, equals);
        while (!name.empty() && (name.back() == ' ' || name.back() == '\t')) name.pop_back();
        if (name.empty()) continue;

        bool recognised = false;
        for (std::size_t i = 0; i < known_count && !recognised; ++i) {
            recognised = _stricmp(name.c_str(), known[i]) == 0;
        }
        if (recognised) continue;

        Log::Line("[config] [%s] %s is not a key this mod reads, so it does nothing. Check the "
                  "spelling and the section it is under.", section, name.c_str());
    }
}

// Every key LoadConfig reads, by section. "Smoothing" is listed as known in the
// two sections it could appear in so a retired key gets WarnRetiredSmoothingKey's
// specific explanation rather than the generic "not a key this mod reads".
void WarnUnknownKeys(const std::string& path) {
    static const char* const kNetwork[]  = { "UdpPort" };
    static const char* const kGeneral[]  = { "EnableOnStartup" };
    static const char* const kCamera[]   = { "FovScale" };
    static const char* const kHotkeys[]  = { "ToggleKey", "CycleModeKey", "YawModeKey",
                                             "ChordToggleKey", "ChordCycleModeKey",
                                             "ChordYawModeKey" };
    static const char* const kRotation[] = { "YawSensitivity", "PitchSensitivity",
                                             "RollSensitivity", "InvertYaw", "InvertPitch",
                                             "InvertRoll", "LocalSmoothing", "RemoteSmoothing",
                                             "Smoothing" };
    static const char* const kPosition[] = { "Enabled", "SensitivityX", "SensitivityY",
                                             "SensitivityZ", "InvertX", "InvertY", "InvertZ",
                                             "LimitX", "LimitY", "LimitZ", "LimitZBack",
                                             "Smoothing" };

    WarnUnknownKeys(path, kSectionNetwork,  kNetwork,  sizeof(kNetwork) / sizeof(kNetwork[0]));
    WarnUnknownKeys(path, kSectionGeneral,  kGeneral,  sizeof(kGeneral) / sizeof(kGeneral[0]));
    WarnUnknownKeys(path, kSectionCamera,   kCamera,   sizeof(kCamera) / sizeof(kCamera[0]));
    WarnUnknownKeys(path, kSectionHotkeys,  kHotkeys,  sizeof(kHotkeys) / sizeof(kHotkeys[0]));
    WarnUnknownKeys(path, kSectionRotation, kRotation, sizeof(kRotation) / sizeof(kRotation[0]));
    WarnUnknownKeys(path, kSectionPosition, kPosition, sizeof(kPosition) / sizeof(kPosition[0]));
}

}  // namespace

void LoadConfig(const std::string& exe_dir, Config& out) {
    const std::string path = IniPath(exe_dir);
    cameraunlock::IniReader ini;
    if (!ini.Open(path)) {
        Log::Line("[config] could not open %s - using built-in defaults", path.c_str());
        return;
    }

    WarnUtf8Bom(path);
    WarnUnknownKeys(path);

    // The bindings this load started from, so a pair of keys that collides can
    // be put back to something that does not.
    const Config previous = out;

    ReadUdpPort(ini, out);

    out.enable_on_startup  = ReadFlag(ini, kSectionGeneral, "EnableOnStartup",  out.enable_on_startup);

    out.fov_scale = ReadFloatValue(ini, kSectionCamera, "FovScale", out.fov_scale,
                                   [](float raw) { return SanitizeFovScale(raw); });

    out.toggle_key            = ReadKey(ini, "ToggleKey",         out.toggle_key);
    out.cycle_mode_key        = ReadKey(ini, "CycleModeKey",      out.cycle_mode_key);
    out.chord_toggle_key      = ReadKey(ini, "ChordToggleKey",    out.chord_toggle_key);
    out.chord_cycle_mode_key  = ReadKey(ini, "ChordCycleModeKey", out.chord_cycle_mode_key);
    out.yaw_mode_key          = ReadKey(ini, "YawModeKey",        out.yaw_mode_key);
    out.chord_yaw_mode_key    = ReadKey(ini, "ChordYawModeKey",   out.chord_yaw_mode_key);
    RefuseCollidingHotkeys(previous, out);

    out.yaw_sensitivity    = ReadSensitivity(ini, kSectionRotation, "YawSensitivity",   out.yaw_sensitivity);
    out.pitch_sensitivity  = ReadSensitivity(ini, kSectionRotation, "PitchSensitivity", out.pitch_sensitivity);
    out.roll_sensitivity   = ReadSensitivity(ini, kSectionRotation, "RollSensitivity",  out.roll_sensitivity);
    out.invert_yaw         = ReadFlag(ini, kSectionRotation, "InvertYaw",        out.invert_yaw);
    out.invert_pitch       = ReadFlag(ini, kSectionRotation, "InvertPitch",      out.invert_pitch);
    out.invert_roll        = ReadFlag(ini, kSectionRotation, "InvertRoll",       out.invert_roll);
    out.local_smoothing    = ReadSmoothing(ini, kSectionRotation, "LocalSmoothing",  out.local_smoothing,
                                           kDefaultLocalSmoothing);
    out.remote_smoothing   = ReadSmoothing(ini, kSectionRotation, "RemoteSmoothing", out.remote_smoothing,
                                           kDefaultRemoteSmoothing);
    WarnRetiredSmoothingKey(ini, kSectionRotation, "Smoothing");
    WarnRetiredSmoothingKey(ini, kSectionPosition, "Smoothing");

    out.position_enabled   = ReadFlag(ini, kSectionPosition, "Enabled",          out.position_enabled);
    out.position_sensitivity_x = ReadSensitivity(ini, kSectionPosition, "SensitivityX", out.position_sensitivity_x);
    out.position_sensitivity_y = ReadSensitivity(ini, kSectionPosition, "SensitivityY", out.position_sensitivity_y);
    out.position_sensitivity_z = ReadSensitivity(ini, kSectionPosition, "SensitivityZ", out.position_sensitivity_z);
    out.invert_position_x  = ReadFlag(ini, kSectionPosition, "InvertX",          out.invert_position_x);
    out.invert_position_y  = ReadFlag(ini, kSectionPosition, "InvertY",          out.invert_position_y);
    out.invert_position_z  = ReadFlag(ini, kSectionPosition, "InvertZ",          out.invert_position_z);
    out.limit_x            = ReadLimit(ini, "LimitX",     out.limit_x);
    out.limit_y            = ReadLimit(ini, "LimitY",     out.limit_y);
    out.limit_z            = ReadLimit(ini, "LimitZ",     out.limit_z);
    out.limit_z_back       = ReadLimit(ini, "LimitZBack", out.limit_z_back);
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
