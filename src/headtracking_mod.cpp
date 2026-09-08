// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "headtracking_mod.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cmath>
#include <string>

#include "builds/build_registry.h"
#include "camera_hook.h"
#include "camera_transform.h"
#include "config.h"
#include "game_state.h"
#include "hotkey_names.h"
#include "logging.h"
#include "window_centering.h"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/input/hotkey_poller.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/os/module_paths.h"
#include "cameraunlock/protocol/udp_receiver.h"
#include "cameraunlock/time/frame_clock.h"
#include "cameraunlock/tracking/head_tracking_session.h"

namespace sr_ht {

namespace {

using Session = cameraunlock::HeadTrackingSession<cameraunlock::UdpReceiver>;
// Without IsRemoteConnection() on the receiver the session silently falls back
// to LocalSmoothing forever, with nothing at the call site to show it.
static_assert(Session::kHasRemoteConnection,
              "UdpReceiver must expose IsRemoteConnection() to select Local/RemoteSmoothing");

Config g_config;
cameraunlock::UdpReceiver g_receiver;
Session g_session(g_receiver);
cameraunlock::input::HotkeyPoller g_hotkeys;
cameraunlock::time::FrameClock g_frame_clock;

std::atomic<bool> g_tracking_enabled{false};

// The one flag that publishes everything the bootstrap thread built - the
// Config and the session's sensitivity, smoothing and position settings to the
// render thread that reads them. Nothing else synchronises the two, so the
// store is a release and every load an acquire:
// under relaxed ordering the render thread is free to observe the flag without
// observing the state it stands for, and would compose a pose from
// half-initialised settings.
std::atomic<bool> g_active{false};
std::atomic<long long> g_frame_counter{0};

// Whether this module is pinned against unloading - see PinModule. Recorded at
// load and reported from the bootstrap, which is the first point there is a log
// to report it into.
std::atomic<bool> g_pinned{false};

// ---------------------------------------------------------------------------
// Config to pipeline
// ---------------------------------------------------------------------------

// Translation only, from the INI-backed Config into the core pipeline's own
// settings types. Both arguments are explicit rather than reaching for the file
// statics, so this reads as - and can be reasoned about as - a mapping with no
// other reach into the mod's state.
void ApplyConfigToPipeline(const Config& config, Session& session) {
    cameraunlock::SensitivitySettings sensitivity;
    sensitivity.yaw = config.yaw_sensitivity;
    sensitivity.pitch = config.pitch_sensitivity;
    sensitivity.roll = config.roll_sensitivity;
    sensitivity.invert_yaw = config.invert_yaw;
    sensitivity.invert_pitch = config.invert_pitch;
    sensitivity.invert_roll = config.invert_roll;
    session.GetProcessor().SetSensitivity(sensitivity);

    // One pair of values for rotation and position alike. The session owns them
    // and recomposes them onto whatever position settings it is handed, so the
    // two calls below compose in either order and no settings rebuild can drop
    // them. Which of the two is in effect is decided per connection from the
    // receiver's source-address check, so nothing here picks one.
    session.SetLocalSmoothing(config.local_smoothing);
    session.SetRemoteSmoothing(config.remote_smoothing);

    session.SetPositionSettings(cameraunlock::PositionSettings::Symmetric(
        config.position_sensitivity_x,
        config.position_sensitivity_y,
        config.position_sensitivity_z,
        config.limit_x, config.limit_y, config.limit_z, config.limit_z_back,
        config.local_smoothing, config.remote_smoothing,
        config.invert_position_x, config.invert_position_y, config.invert_position_z));

    session.SetMode(config.position_enabled ? cameraunlock::TrackingMode::RotationAndPosition
                                            : cameraunlock::TrackingMode::RotationOnly);
}

// ---------------------------------------------------------------------------
// Render-thread diagnostics
//
// Every function here is edge-triggered or bounded. This is the render path,
// and a line per frame is a log nobody can read.
// ---------------------------------------------------------------------------

// The receiver reports its last known pose for as long as the process lives, so
// GetRotation() answering true says a packet arrived once, not that a tracker is
// still sending. IsReceiving() is the freshness check, and its two edges are
// what a "head tracking just stopped" report turns on.
//
// Capped: the freshness window is 500ms, so a tracker sending slower than that
// crosses this edge on every packet, and an unbounded pair of lines per packet
// is the render-path log nobody can read.
constexpr int kConnectionChangesLogged = 10;

void LogTrackerConnection() {
    static bool last_receiving = false;
    static int logged = 0;

    const bool receiving = g_receiver.IsReceiving();
    if (receiving == last_receiving) return;
    last_receiving = receiving;

    if (logged >= kConnectionChangesLogged) return;
    ++logged;
    Log::Line("[udp] tracker data %s%s",
              receiving ? "is arriving" : "stopped arriving - holding the last pose",
              logged == kConnectionChangesLogged
                  ? " (this log is capped; further changes are not reported)" : "");
}

// The session re-reads the receiver's source-address check every update, so a
// player who switches from a local OpenTrack instance to a phone on WiFi
// mid-session gets the other smoothing parameter without restarting the game.
// This only records the switch, so a bug report can say which of the two values
// was actually in effect.
void LogConnectionLocality() {
    static bool last_remote = false;
    static bool known = false;

    const bool is_remote = g_session.IsRemoteConnection();
    if (known && is_remote == last_remote) return;
    last_remote = is_remote;
    known = true;

    Log::Line("[udp] tracker source is %s - smoothing=%.2f",
              is_remote ? "a remote device" : "on this machine",
              cameraunlock::math::GetEffectiveSmoothing(
                  g_config.local_smoothing, g_config.remote_smoothing, is_remote));
}

// Rate-limited rather than capped at a count. A co-op session that ticks
// unevenly around the 5s hold flips this edge for as long as the session lasts,
// so an unbounded line per flip is the render-path log nobody can read - but a
// hard cap is worse here than anywhere else in this file: the README tells a
// player whose co-op gate misbehaves to send exactly this line in, and the gate
// has never been exercised in a live session. A cap of ten would have gone
// silent seconds into the only session anyone ever reports on. One line per
// interval keeps the whole session legible without filling the file.
//
// Triggered on the GATE only, not on whether tracking is following: the toggle
// hotkey changes that, and ToggleTracking already reports it from the other
// side, so including it here logged every keypress twice.
constexpr long long kGateChangeIntervalMs = 5000;

void LogGateChange(bool multiplayer) {
    static bool last_multiplayer = false;
    static bool known = false;
    static long long next_tick = 0;

    if (known && multiplayer == last_multiplayer) return;

    const long long now = static_cast<long long>(GetTickCount64());
    if (known && now < next_tick) return;
    next_tick = now + kGateChangeIntervalMs;

    last_multiplayer = multiplayer;
    known = true;

    // Says what the GATE is doing, not what tracking is doing: the toggle
    // hotkey can have tracking off while the gate is wide open, and this line
    // cannot see that.
    Log::Line("[state] co-op session %s - the gate is %s", multiplayer ? "LIVE" : "not running",
              multiplayer ? "holding the camera still" : "letting head tracking through");
}

// Latched and reported ahead of the gate so the log records that tracker data
// reached the render hook even if the gameplay gate then rejects it.
void LogFirstPoseReachingCamera(long long frame, bool have_rotation, const HeadPose& pose) {
    static bool logged = false;
    if (logged || !have_rotation) return;
    logged = true;

    Log::Line("[camera] head pose reached the camera hook on frame %lld: "
              "yaw=%.2f pitch=%.2f roll=%.2f", frame, pose.yaw, pose.pitch, pose.roll);
}

// How far off centre a pose has to be before it counts as the player having
// deliberately moved, rather than the jitter a tracker emits sitting still.
constexpr float kDeliberateMovementDegrees = 5.0f;

// Once, for the first pose that both cleared the gate and was big enough to see.
// This is the line that separates "the tracker is not reaching the camera" from
// "the camera is not reaching the screen", which is the whole of triaging a
// report that head tracking does nothing.
void LogFirstComposedPose(const HeadPose& pose) {
    static bool logged = false;
    if (logged) return;
    if (std::fabs(pose.yaw) < kDeliberateMovementDegrees
        && std::fabs(pose.pitch) < kDeliberateMovementDegrees) {
        return;
    }
    logged = true;
    Log::Line("[camera] composed a head pose into the frame: yaw=%.2f pitch=%.2f roll=%.2f "
              "lean=%.3f %.3f %.3f", pose.yaw, pose.pitch, pose.roll,
              pose.lean_x, pose.lean_y, pose.lean_z);
}

// ---------------------------------------------------------------------------
// Hotkeys
// ---------------------------------------------------------------------------

void ToggleTracking() {
    const bool on = !g_tracking_enabled.load();
    g_tracking_enabled.store(on);
    Log::Line("[input] tracking %s", on ? "enabled" : "disabled");
}

// World up by default. The chase camera looks down at the truck, and yawing
// about its own up axis in that attitude sweeps the view around a cone instead
// of turning it. Camera-local is the other position because the cabin camera
// banks with the truck, and a truck that has landed on its roof has a world up
// that means nothing to the driver.
std::atomic<bool> g_world_yaw{true};

void ToggleYawMode() {
    const bool world = !g_world_yaw.load();
    g_world_yaw.store(world);
    Log::Line("[input] head yaw turns about %s", world ? "world up" : "the camera's own up axis");
}

void CycleTrackingMode() {
    const char* name = "";
    switch (g_session.CycleMode()) {
        case cameraunlock::TrackingMode::RotationAndPosition: name = "rotation and position"; break;
        case cameraunlock::TrackingMode::RotationOnly:        name = "rotation only"; break;
        case cameraunlock::TrackingMode::PositionOnly:        name = "position only"; break;
    }
    Log::Line("[input] tracking mode: %s", name);
}

// Every action is reachable two ways: its nav-cluster key, and the
// Ctrl+Shift+<letter> chord for keyboards without a nav cluster. Pairing them in
// one row is what keeps the two lists from drifting apart, and the line that
// tells the user which keys they ended up on is built from these same rows.
struct HotkeyBinding {
    const char* action;
    int nav_key;
    int chord_key;
    void (*handler)();
};

std::array<HotkeyBinding, 3> Bindings(const Config& config) {
    return {{
        { "toggle tracking",     config.toggle_key,     config.chord_toggle_key,     ToggleTracking },
        { "cycle tracking mode", config.cycle_mode_key, config.chord_cycle_mode_key, CycleTrackingMode },
        { "toggle yaw mode",     config.yaw_mode_key,   config.chord_yaw_mode_key,   ToggleYawMode },
    }};
}

void RegisterHotkeys(const Config& config) {
    using namespace cameraunlock::input;
    for (const HotkeyBinding& binding : Bindings(config)) {
        g_hotkeys.AddHotkey(binding.nav_key, NavGuarded(binding.handler));
        g_hotkeys.AddHotkey(binding.chord_key, ChordGuarded(binding.handler));
    }
    g_hotkeys.Start();
}

void LogHotkeys(const Config& config) {
    std::string ready = "[boot] ready.";
    for (const HotkeyBinding& binding : Bindings(config)) {
        ready += " " + HotkeyName(binding.nav_key) + "/Ctrl+Shift+"
               + HotkeyName(binding.chord_key) + " " + binding.action + ",";
    }
    ready.back() = '.';

    // Through %s, never as the format itself: the names come from the keyboard
    // layout, and a layout that names a key with a '%' would otherwise turn this
    // line into a format string reading arguments that were never passed.
    Log::Line("%s", ready.c_str());
}

// ---------------------------------------------------------------------------
// Bootstrap
// ---------------------------------------------------------------------------

bool OpenLogAndResolveGameDirectory(std::string& exe_dir) {
    // The core's resolver rather than a local copy of it: it grows its buffer
    // past MAX_PATH, so a game under a long install path resolves instead of
    // leaving the mod dormant, and it refuses a best-fit ANSI narrowing rather
    // than handing back the name of a different directory that happens to exist.
    const std::wstring exe_dir_wide = cameraunlock::os::HostExeDirectory();

    // Beside the game EXE, not in the process working directory: a launcher can
    // start the game from anywhere, and a bare relative name then drops the log
    // wherever that happens to be.
    Log::Open(exe_dir_wide.empty() ? std::wstring(L"HeadTracking.log")
                                   : exe_dir_wide + L"\\HeadTracking.log");
    Log::Line("=== SnowRunner Head Tracking ===");

    // The INI layer is ANSI-only (IniReader wraps GetPrivateProfile*A), so a
    // directory with no ANSI form is as unusable as one that would not resolve.
    if (exe_dir_wide.empty() || !cameraunlock::os::NarrowToAnsi(exe_dir_wide, exe_dir)) {
        Log::Line("[boot] could not resolve the game directory - mod is dormant, game runs vanilla.");
        return false;
    }
    Log::Line("[boot] game directory: %s", exe_dir.c_str());
    return true;
}

void LoadAndApplyConfig(const std::string& exe_dir) {
    WriteDefaultConfigIfMissing(exe_dir);
    LoadConfig(exe_dir, g_config);
    Log::Line("[boot] config: port=%u enableOnStartup=%d localSmoothing=%.2f "
              "remoteSmoothing=%.2f position=%d fovScale=%.2f",
              static_cast<unsigned>(g_config.udp_port), g_config.enable_on_startup ? 1 : 0,
              g_config.local_smoothing, g_config.remote_smoothing,
              g_config.position_enabled ? 1 : 0, g_config.fov_scale);

    ApplyConfigToPipeline(g_config, g_session);
    g_tracking_enabled.store(g_config.enable_on_startup);
}

void StartReceiver() {
    g_receiver.SetLog([](const std::string& msg) { Log::Line("[udp] %s", msg.c_str()); });
    if (g_receiver.Start(g_config.udp_port)) {
        Log::Line("[boot] listening for OpenTrack data on UDP %u",
                  static_cast<unsigned>(g_config.udp_port));
    }
}

// Takes a reference on this module so an explicit FreeLibrary cannot unmap it.
// Three things outlive such a call: the bootstrap thread, the hotkey and
// receiver threads, and - the one that is fatal - an inline detour sitting in
// the game's camera update, which a thread can be executing at the moment the
// pages go away. Undoing all of that would have to happen in DllMain, under the
// loader lock, where joining a thread deadlocks: a thread cannot exit without
// taking the same lock DllMain is holding. So there is no teardown at all, and
// refusing the unload is what makes that safe. The process exiting reclaims
// everything.
bool PinModule() {
    HMODULE self = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                  | GET_MODULE_HANDLE_EX_FLAG_PIN,
                              reinterpret_cast<LPCWSTR>(&ApplyConfigToPipeline),
                              &self) != FALSE;
}

void Bootstrap() {
    std::string exe_dir;
    if (!OpenLogAndResolveGameDirectory(exe_dir)) return;

    // Before any hook goes in, and fatal rather than advisory. An inline detour
    // in the game's camera update outlives an unload: a thread can be executing
    // inside it at the moment the pages go away. The mod has no safe teardown to
    // fall back on either - undoing hooks and joining threads would have to
    // happen in DllMain, under the loader lock, where a join deadlocks. So the
    // pin is the whole safety story, and without it the answer is to do nothing.
    if (!g_pinned.load()) {
        Log::Line("[boot] this module could not be pinned against unloading, so a FreeLibrary "
                  "could unmap the camera hook while the game is running - mod is dormant, "
                  "game runs vanilla.");
        return;
    }

    if (builds::SelectProfile(GetModuleHandleW(nullptr)) != builds::ProfileSelection::Matched) {
        Log::Line("[boot] no usable build profile - mod is dormant, game runs vanilla.");
        return;
    }
    if (!InitGameState()) {
        Log::Line("[boot] the co-op gate could not be resolved - mod is dormant, game runs "
                  "vanilla. Head tracking in a co-op session is not something to guess at.");
        return;
    }

    LoadAndApplyConfig(exe_dir);
    StartReceiver();

    if (!InstallCameraHook(g_config.fov_scale)) {
        // Nothing will ever read the tracker now, so give the port back rather
        // than sitting on it for the rest of the session and blocking whatever
        // else the user points their tracker at.
        g_receiver.Stop();
        ShutdownGameState();
        Log::Line("[boot] the camera update could not be hooked - mod is inert.");
        return;
    }

    RegisterHotkeys(g_config);
    g_active.store(true, std::memory_order_release);
    LogHotkeys(g_config);

    // Last, because it blocks for as long as the engine takes to place its
    // window: the hook, the receiver and the hotkeys are all live before this
    // waits on anything. It is also below the dormant returns above on purpose -
    // a build this mod does not know leaves the game entirely alone, and moving
    // the player's window would be the one thing it still did.
    CenterWindowWhenReady();
}

}  // namespace

bool WorldYawEnabled() { return g_world_yaw.load(std::memory_order_relaxed); }

bool PoseForThisFrame(HeadPose& pose) {
    if (!g_active.load(std::memory_order_acquire)) return false;

    const float dt = g_frame_clock.Tick();

    // The pipeline advances whatever the gate says. Freezing it in a menu and
    // thawing it on the road would compose a pose from wherever the head was
    // when the player last drove; advancing it means the first tracked frame is
    // composed from where the head is now, with nothing to jump from.
    if (g_session.Update(dt)) LogConnectionLocality();
    LogTrackerConnection();

    const long long frame = g_frame_counter.fetch_add(1, std::memory_order_relaxed);

    const bool have_rotation = g_session.GetRotation(pose.yaw, pose.pitch, pose.roll);
    g_session.GetPositionOffset(pose.lean_x, pose.lean_y, pose.lean_z);

    LogFirstPoseReachingCamera(frame, have_rotation, pose);

    const bool multiplayer = IsMultiplayerSessionLive();
    const bool following = ShouldFollowHead(
        g_tracking_enabled.load(std::memory_order_relaxed), multiplayer);
    LogGateChange(multiplayer);

    // No tracker data, or the gate is shut: leave the engine's camera exactly as
    // it computed it.
    if (!have_rotation || !following) return false;

    LogFirstComposedPose(pose);
    return true;
}

DWORD WINAPI BootstrapThread(LPVOID) {
    Bootstrap();
    return 0;
}

void Initialize() {
    // Before anything else, and before any thread exists to be caught by it.
    g_pinned.store(PinModule());

    // On a new thread: DllMain runs under the loader lock, so the bootstrap
    // (which opens a log, reads the INI and resolves engine classes) cannot run
    // here. CreateThread rather than std::thread because this is DllMain - a
    // std::thread that cannot start throws std::system_error, and an exception
    // leaving DllMain calls std::terminate before the log is even open, killing
    // the game at startup with nothing written to explain it. CreateThread
    // returns null instead, and a mod that cannot bootstrap leaves the game
    // running vanilla, which is the dormancy contract everywhere else here.
    const HANDLE thread = CreateThread(nullptr, 0, &BootstrapThread, nullptr, 0, nullptr);
    if (thread != nullptr) CloseHandle(thread);
}

}  // namespace sr_ht
