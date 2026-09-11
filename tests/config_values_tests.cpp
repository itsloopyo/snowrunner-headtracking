// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

// What HeadTracking.ini does with a value the mod cannot use, and - the half
// that had no cover at all - whether it SAYS so.
//
// The value side was already right: every reader in IniReader answers
// unparseable text with the fallback its caller passed, and the loader passes
// the value the Config already holds, so a refused key keeps what it had. What
// was missing was the diagnostic. A refused value looked exactly like a key the
// user never wrote, so "my INI setting is ignored" arrived with an empty log
// and nothing to triage from, while every other refusal in config.cpp - a bad
// port, a bad hotkey, an out-of-range float - reported itself.
//
// The trap is a bool with a trailing comment. GetPrivateProfileString does not
// treat ';' as a comment introducer and ReadBool matches the WHOLE value, so
// `Enabled=0 ; no lean` matches nothing and position tracking stays on.
//
// The other half of this file is the regression risk the fix carries: the
// detector must not cry wolf on values that are perfectly legitimate.

#include "config.h"
#include "config_sanitize.h"
#include "logging.h"

#include "ini_fixture.h"
#include "test_support.h"

#include <windows.h>

#include <cstdio>
#include <share.h>
#include <string>

using namespace sr_ht;
using sr_test::Check;
using sr_test::CheckClose;

namespace {

std::string g_dir;

// Opens the log config.cpp writes its refusals to, inside the fixture's
// directory, so the checks below can read back what a load said.
//
// Widened through the API rather than by copying chars: a temp path under a
// non-ASCII user name would otherwise reach Log::Open as a different path, the
// log would land somewhere else, and every check below would fail with nothing
// to say why.
bool OpenLogInTempDir() {
    const std::string path = sr_test::LogPathIn(g_dir);
    const int wide_length = MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, nullptr, 0);
    if (wide_length <= 0) {
        std::printf("  FAIL: could not widen %s\n", path.c_str());
        ++sr_test::g_failures;
        return false;
    }
    std::wstring wide(static_cast<size_t>(wide_length) - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, path.c_str(), -1, &wide[0], wide_length);
    Log::Open(wide);
    return true;
}

std::string ReadWholeLog() {
    // _fsopen, not fopen_s: fopen_s denies sharing by default, so it cannot open
    // a file the log still holds a write handle on and every read came back
    // empty - which reads as "nothing was logged" and quietly passes every
    // check that asserts silence.
    FILE* f = _fsopen(sr_test::LogPathIn(g_dir).c_str(), "rb", _SH_DENYNO);
    if (f == nullptr) return {};
    std::string text;
    char chunk[4096];
    size_t got = 0;
    while ((got = std::fread(chunk, 1, sizeof(chunk), f)) > 0) text.append(chunk, got);
    std::fclose(f);
    return text;
}

// A Config loaded from `body`, plus only the log lines that load produced.
struct Load {
    Config config;
    std::string log;
};

Load LoadIni(const char* body) {
    const size_t logBefore = ReadWholeLog().size();
    if (!sr_test::WriteIni(g_dir, body)) return {};

    Load result;
    LoadConfig(g_dir, result.config);
    result.log = ReadWholeLog().substr(logBefore);
    return result;
}

bool Mentions(const std::string& log, const char* key) {
    return log.find(key) != std::string::npos;
}

// Both halves of one refusal, checked together: the value must not move, and
// the log must name the key. Either alone is the bug.
void CheckRefusedAndReported(const Load& loaded, bool actual, bool expected, const char* key) {
    Check(actual == expected, "the flag keeps its previous value");
    Check(Mentions(loaded.log, key), "and the log names the key that was refused");
}

void CheckRefusedAndReported(const Load& loaded, float actual, float expected, const char* key) {
    CheckClose(actual, expected, "the value keeps its previous value");
    Check(Mentions(loaded.log, key), "and the log names the key that was refused");
}

void AcceptedValuesAreReadAndNotComplainedAbout() {
    // The regression the refusal detector could introduce: a legitimate value
    // reported as unreadable, so the log fills with complaints about settings
    // that are working. Every check here is a value the mod must take silently.
    std::printf("A value the mod can use is taken, and taken quietly\n");

    const Load off = LoadIni("[Position]\nEnabled=0\n");
    Check(off.config.position_enabled == false, "0 turns position tracking off");
    Check(!Mentions(off.log, "Enabled"), "and nothing is said about it");

    const Load word = LoadIni("[Position]\nEnabled=false\n");
    Check(word.config.position_enabled == false, "false turns it off too");
    Check(!Mentions(word.log, "Enabled"), "and nothing is said about that either");

    const Load on = LoadIni("[General]\nEnableOnStartup=off\n");
    Check(on.config.enable_on_startup == false, "off is a spelling of 0");
    Check(!Mentions(on.log, "EnableOnStartup"), "and is taken silently");

    const Load invert = LoadIni("[Rotation]\nInvertPitch=1\n");
    Check(invert.config.invert_pitch == true, "1 sets an invert flag");
    Check(!Mentions(invert.log, "InvertPitch"), "and is taken silently");

    const Load sens = LoadIni("[Rotation]\nYawSensitivity=2.5\n");
    CheckClose(sens.config.yaw_sensitivity, 2.5f, "a plain number is read");
    Check(!Mentions(sens.log, "YawSensitivity"), "and is taken silently");

    // A trailing comment IS survivable on a number - strtod stops at the ';' -
    // so this must go on working rather than be swept up by the new refusal.
    const Load commented = LoadIni("[Rotation]\nPitchSensitivity=1.5 ; boost\n");
    CheckClose(commented.config.pitch_sensitivity, 1.5f,
               "a number with a trailing comment is still read");
    Check(!Mentions(commented.log, "PitchSensitivity"), "and is still taken silently");

    const Load zero = LoadIni("[Position]\nLimitY=0\n");
    CheckClose(zero.config.limit_y, 0.0f, "a zero limit (axis pinned) is a real setting");
    Check(!Mentions(zero.log, "LimitY"), "and is taken silently");

    const Load smoothing = LoadIni("[Rotation]\nLocalSmoothing=0\n");
    CheckClose(smoothing.config.local_smoothing, 0.0f,
               "a configured zero smoothing is a real setting");
    Check(!Mentions(smoothing.log, "LocalSmoothing"), "and is taken silently");

    const Load fov = LoadIni("[Camera]\nFov=95\n");
    CheckClose(fov.config.fov_degrees, 95.0f, "a field of view angle is read");
    Check(!Mentions(fov.log, "Fov"), "and is taken silently");

    const Load port = LoadIni("[Network]\nUdpPort=5005\n");
    Check(port.config.udp_port == 5005, "an in-range port is read");
    Check(!Mentions(port.log, "UdpPort"), "and is taken silently");
}

void RefusedValuesAreReported() {
    std::printf("A value the mod cannot use is kept off, and said out loud\n");
    const Config defaults;

    // The one this exists for.
    const Load commented = LoadIni("[Position]\nEnabled=0 ; no lean\n");
    CheckRefusedAndReported(commented, commented.config.position_enabled,
                            defaults.position_enabled, "Enabled");

    const Load prose = LoadIni("[Rotation]\nInvertYaw=yes please\n");
    CheckRefusedAndReported(prose, prose.config.invert_yaw, defaults.invert_yaw, "InvertYaw");

    const Load word = LoadIni("[Rotation]\nYawSensitivity=high\n");
    CheckRefusedAndReported(word, word.config.yaw_sensitivity,
                            defaults.yaw_sensitivity, "YawSensitivity");

    const Load limit = LoadIni("[Position]\nLimitX=wide\n");
    CheckRefusedAndReported(limit, limit.config.limit_x, defaults.limit_x, "LimitX");

    // A malformed RemoteSmoothing must never land on the LOCAL default: that
    // would hand a phone on WiFi no smoothing at all on raw network jitter.
    const Load remote = LoadIni("[Rotation]\nRemoteSmoothing=nonsense\n");
    CheckRefusedAndReported(remote, remote.config.remote_smoothing, 0.15f, "RemoteSmoothing");
}

// A number with anything after it used to be taken at its numeric PREFIX, with
// no complaint, because the loader decided "did this parse" by reading the key
// twice with different fallbacks and comparing - which only detects text with no
// parseable prefix at all.
//
// The decimal comma is the case that matters. It is the norm across most of
// continental Europe, `LimitX=0,25` silently disabled the X lean, and the log
// said nothing because the parsed 0.0 then equalled its own sanitized value.
void ANumberWithJunkAfterItIsRefusedNotTruncated() {
    std::printf("A value that is only PART of a number is refused, not silently truncated\n");
    const Config defaults;

    const Load comma = LoadIni("[Position]\nLimitX=0,25\n");
    CheckRefusedAndReported(comma, comma.config.limit_x, defaults.limit_x, "LimitX");
    Check(Mentions(comma.log, "comma"),
          "and the log names the comma, which is the one fault a user cannot guess");

    // 2,5 used to arrive as a clean 2.0 - a fifth of the way off, silently.
    const Load sens = LoadIni("[Rotation]\nYawSensitivity=2,5\n");
    CheckRefusedAndReported(sens, sens.config.yaw_sensitivity,
                            defaults.yaw_sensitivity, "YawSensitivity");

    // The remote value is the one that must never fall back to the local
    // default, whatever shape the refusal took.
    const Load smoothing = LoadIni("[Rotation]\nRemoteSmoothing=0,3\n");
    CheckRefusedAndReported(smoothing, smoothing.config.remote_smoothing, 0.15f,
                            "RemoteSmoothing");

    const Load suffix = LoadIni("[Rotation]\nYawSensitivity=2.5abc\n");
    CheckRefusedAndReported(suffix, suffix.config.yaw_sensitivity,
                            defaults.yaw_sensitivity, "YawSensitivity");

    // The integer key has to hold the same contract, or UdpPort becomes the one
    // place a junk suffix is quietly accepted.
    const Load port = LoadIni("[Network]\nUdpPort=5006xyz\n");
    Check(port.config.udp_port == defaults.udp_port, "a junk-suffixed port keeps the default");
    Check(Mentions(port.log, "UdpPort"), "and the log names it");

    // Trailing space and a trailing comment are still legitimate, so the
    // stricter parser must not start crying wolf on them.
    const Load spaced = LoadIni("[Position]\nLimitX=0.25   \n");
    CheckClose(spaced.config.limit_x, 0.25f, "trailing whitespace is still accepted");
    const Load commented = LoadIni("[Position]\nLimitY=0.25 ; a quarter metre\n");
    CheckClose(commented.config.limit_y, 0.25f, "a trailing comment on a NUMBER is accepted");
    const Load exponent = LoadIni("[Rotation]\nYawSensitivity=1.5e0\n");
    CheckClose(exponent.config.yaw_sensitivity, 1.5f, "exponent notation is still a number");
}

// A key the mod does not read is indistinguishable from one it read and ignored.
// Both look like "the mod ignores my setting", and neither said anything.
void AKeyTheModDoesNotReadIsCalledOut() {
    std::printf("A misspelled or misplaced key says so\n");

    const Load typo = LoadIni("[Rotation]\nYawSensitivty=2.5\n");
    Check(Mentions(typo.log, "YawSensitivty"), "a misspelled key is named in the log");

    // The smoothing comment says the value covers position too, so filing it
    // under [Position] is the natural mistake.
    const Load misplaced = LoadIni("[Position]\nRemoteSmoothing=0.3\n");
    Check(Mentions(misplaced.log, "RemoteSmoothing"), "a key under the wrong section is named");
    Check(misplaced.config.remote_smoothing == Config{}.remote_smoothing,
          "and it really was ignored");

    // The retired key keeps its own, more useful explanation rather than being
    // swept up as merely unknown.
    const Load retired = LoadIni("[Rotation]\nSmoothing=0.5\n");
    Check(Mentions(retired.log, "retired"), "the retired Smoothing key keeps its own message");

    // And a file that says exactly what the mod reads says nothing at all.
    const Load clean = LoadIni("[Rotation]\nYawSensitivity=1.5\n[Position]\nLimitX=0.25\n");
    Check(!Mentions(clean.log, "is not a key"), "a file of known keys draws no complaint");

    // A commented-out key must not be reported as an unknown one.
    // GetPrivateProfileSectionA drops ';' lines but hands back '#' lines
    // verbatim, and this mod honours '#' as a comment introducer everywhere
    // else - so the unknown-key walk has to skip them itself. Without that,
    // `# LimitX=0.25 was my old value` produced a confident complaint about a
    // key named "# LimitX".
    const Load hashComment = LoadIni("[Position]\n# LimitX=0.25 was my old value\nLimitX=0.3\n");
    Check(!Mentions(hashComment.log, "is not a key"),
          "a # commented-out key draws no complaint");
    CheckClose(hashComment.config.limit_x, 0.3f, "and the real key beneath it is still read");

    const Load semiComment = LoadIni("[Position]\n; LimitY=0.25 was my old value\nLimitY=0.22\n");
    Check(!Mentions(semiComment.log, "is not a key"),
          "and neither does a ; commented-out key");
    CheckClose(semiComment.config.limit_y, 0.22f, "with its real key read too");
}

void AbsentKeysAreSilent() {
    // The other side of the same coin: a key the user never wrote is not a
    // refusal, and reporting it would bury the ones that are.
    std::printf("A key the file never mentions says nothing\n");
    const Config defaults;

    const Load empty = LoadIni("[Network]\nUdpPort=4242\n");
    Check(empty.config.position_enabled == defaults.position_enabled,
          "an unmentioned flag keeps its default");
    Check(!Mentions(empty.log, "Enabled"), "and is not reported as refused");
    Check(!Mentions(empty.log, "LimitZ"), "nor is an unmentioned limit");

    const Load blank = LoadIni("[General]\nEnableOnStartup=\n");
    Check(blank.config.enable_on_startup == defaults.enable_on_startup,
          "a key with an empty value keeps its default");
    Check(!Mentions(blank.log, "EnableOnStartup"),
          "and an empty value is treated as unset, not as garbage");
}

void BoundaryChecksStillRun() {
    // The refusal sits in front of the range checks, not instead of them.
    std::printf("What does parse still goes through the boundary checks\n");
    const Config defaults;

    CheckClose(LoadIni("[Rotation]\nRemoteSmoothing=5.0\n").config.remote_smoothing, 1.0f,
               "a smoothing above 1 still clamps");
    CheckClose(LoadIni("[Position]\nLimitY=-3\n").config.limit_y, 0.0f,
               "a negative limit still clamps to zero");
    CheckClose(LoadIni("[Camera]\nFov=1.6\n").config.fov_degrees, kMinFov,
               "a Fov typed as a multiplier still clamps to the narrow bound");
    CheckClose(LoadIni("[Position]\nLimitZBack=1e400\n").config.limit_z_back,
               defaults.limit_z_back,
               "a value that overflows to infinity still falls back");
    CheckClose(LoadIni("[Rotation]\nRollSensitivity=1e30\n").config.roll_sensitivity, 100.0f,
               "an absurd sensitivity still clamps");

    // 70000 & 0xFFFF is 4464 - a port the mod would bind and then report as the
    // one that was asked for.
    Check(LoadIni("[Network]\nUdpPort=70000\n").config.udp_port == defaults.udp_port,
          "a port above 65535 falls back rather than truncating to 4464");
    Check(LoadIni("[Network]\nUdpPort=80\n").config.udp_port == defaults.udp_port,
          "a privileged port falls back");
    Check(LoadIni("[Network]\nUdpPort=abc\n").config.udp_port == defaults.udp_port,
          "unreadable text falls back rather than binding port 0");
}

void ThePortRefusalNamesTheRightFault() {
    // ReadInt answers a present-but-unparseable value with 0 rather than the
    // fallback (IniReader's header, rule 4), and NormalizeUdpPort then refuses
    // that 0 for being out of range. Reporting it as a range problem sends the
    // user to check the one thing that was never wrong.
    std::printf("A refused UdpPort says which of the two faults it was\n");

    const Load garbage = LoadIni("[Network]\nUdpPort=abc\n");
    Check(Mentions(garbage.log, "not a number"),
          "text with no number in it is reported as unreadable");
    Check(!Mentions(garbage.log, "outside 1024-65535"),
          "and not as a value outside the range");

    const Load range = LoadIni("[Network]\nUdpPort=80\n");
    Check(Mentions(range.log, "outside 1024-65535"),
          "a real number outside the range is reported as such");

    const Load absent = LoadIni("[General]\nEnableOnStartup=1\n");
    Check(!Mentions(absent.log, "UdpPort"),
          "and a file that never mentions the key says nothing about it");
}

}  // namespace

int main() {
    std::printf("SnowRunner head tracking - config value tests\n");
    std::printf("===================================================\n");
    g_dir = sr_test::MakeTempDir("values");
    if (g_dir.empty() || !OpenLogInTempDir()) return sr_test::Summary("config values");
    AcceptedValuesAreReadAndNotComplainedAbout();
    RefusedValuesAreReported();
    ANumberWithJunkAfterItIsRefusedNotTruncated();
    AKeyTheModDoesNotReadIsCalledOut();
    AbsentKeysAreSilent();
    BoundaryChecksStillRun();
    ThePortRefusalNamesTheRightFault();

    // Before the directory goes: the log still holds a write handle on a file
    // inside it, and a file that is open refuses to delete.
    Log::Close();
    sr_test::RemoveTempDir(g_dir);
    return sr_test::Summary("config values");
}
