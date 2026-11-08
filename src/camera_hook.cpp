// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_hook.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

#include "builds/build_registry.h"
#include "builds/render_discovery.h"
#include "camera_fov.h"
#include "camera_transform.h"
#include "headtracking_mod.h"
#include "logging.h"
#include "player_camera_aim.h"
#include "ui_camera.h"
#include "vehicle_camera_gate.h"

#include "cameraunlock/hooks/hook_manager.h"

namespace sr_ht {

namespace {

using DriveCameraUpdateFn = void(__fastcall*)(void*, void*);
using RenderCameraUploadFn = void(__fastcall*)(void*, void*, void*, int);
using CameraFrustumFn = void*(__fastcall*)(void*, void*, float);
using PlayerCameraGetterFn = void*(__fastcall*)();
using MatrixInverseFn = void(__fastcall*)(void*, int, void*);
using ViewRaysFn = void(__fastcall*)(void*, void*);
using BoundsFn = void(__fastcall*)(void*, void*, void*, float);
using RenderCameraUploadAltFn = void(__fastcall*)(void*, void*, void*);
using UiCameraRefreshFn = void(__fastcall*)(void*);
using LookAtFn = void(__fastcall*)(void*, const float*, const float*, const float*);

DriveCameraUpdateFn g_original_drive_camera_update = nullptr;
RenderCameraUploadFn g_original_render_camera_upload = nullptr;
CameraFrustumFn g_original_camera_frustum = nullptr;
PlayerCameraGetterFn g_player_camera = nullptr;
MatrixInverseFn g_matrix_inverse = nullptr;
ViewRaysFn g_original_view_rays = nullptr;
BoundsFn g_original_bounds = nullptr;
RenderCameraUploadAltFn g_original_render_camera_upload_alt = nullptr;
UiCameraRefreshFn g_original_ui_camera_refresh = nullptr;
LookAtFn g_original_look_at = nullptr;

std::uintptr_t g_module_base = 0;
std::uintptr_t g_frustum_frame_return = 0;
unsigned g_view_offset = 0;
unsigned g_projection_offset = 0;
unsigned g_eye_offset = 0;
unsigned g_view_projection_offset = 0;
unsigned g_inverse_view_offset = 0;
unsigned g_vertical_fov_offset = 0;
std::uintptr_t g_motion_blur_rays_return = 0;
constexpr unsigned kRenderCameraBytes = 0x134;
unsigned g_ui_camera_record = 0;

// The HUD's camera, handed to ui3D_CAMERA_MR in place of the game's snapshot.
// Two, written alternately, so a projection still running from last frame's
// refresh never reads one being rewritten.
alignas(16) std::uint8_t g_ui_cameras[2][kRenderCameraBytes];
std::atomic<unsigned> g_ui_camera_next{0};

std::atomic<bool> g_logged_drive_camera_activity{false};
VehicleCameraGate g_vehicle_gate;
PlayerCameraAimGate g_aim_gate;
bool g_logged_aim_state = false;
bool g_aim_was_active = false;

// CameraUnlock.ini's Fov, the angle the frame should span across its width. Written once
// by InstallCameraHook before the first detour exists, so the render threads
// that read it can never see a half-built value.
float g_fov_degrees = 0.0f;

std::mutex g_pose_mutex;
HeadPose g_render_pose;
bool g_have_render_pose = false;
bool g_render_world_yaw = false;

void __fastcall LookAtDetour(void* record, const float* eye, const float* target, const float* up) {
    g_original_look_at(record, eye, target, up);
    if (record == g_player_camera()) g_aim_gate.OnAim();
}

void __fastcall DriveCameraUpdateDetour(void* camera, void* frame) {
    g_original_drive_camera_update(camera, frame);
    g_vehicle_gate.OnDriveUpdate();
    if (!g_logged_drive_camera_activity.exchange(true, std::memory_order_relaxed)) {
        Log::Line("[camera] drive camera active");
    }
}

bool DrawingVehicleCamera() { return g_vehicle_gate.Active() && g_aim_gate.Active(); }

// This frame's head pose, and whether the camera has to be composed at all.
//
// A frame with no pose still composes while Fov is set: the field of view
// is a rendering setting rather than head tracking, so it survives the toggle
// hotkey, a tracker that has stopped sending and the co-op gate - all three of
// which only decide whether a pose exists. Both stop wherever the player view
// is not the vehicle camera's: a menu, a loading screen, the map.
bool ShouldComposeCamera(std::uintptr_t caller, HeadPose& pose, bool& world_yaw) {
    std::lock_guard<std::mutex> lock(g_pose_mutex);
    if (caller == g_frustum_frame_return) {
        // Visibility must decide the frame and capture the pose before any draw
        // uploads use them.
        g_vehicle_gate.OnFrame();
        g_aim_gate.OnFrame();
        if (g_vehicle_gate.Active() && (!g_logged_aim_state || g_aim_gate.Active() != g_aim_was_active)) {
            Log::Line(g_aim_gate.Active()
                          ? "[camera] the vehicle camera is drawing the player view"
                          : "[camera] the player view is drawn by a camera other than the vehicle camera, "
                            "such as the map's: nothing is composed into it");
            g_logged_aim_state = true;
            g_aim_was_active = g_aim_gate.Active();
        }
        g_have_render_pose = DrawingVehicleCamera() && PoseForThisFrame(g_render_pose);
        g_render_world_yaw = WorldYawEnabled();
    }
    if (!DrawingVehicleCamera()) return false;
    if (!g_have_render_pose) return g_fov_degrees > 0.0f;
    pose = g_render_pose;
    world_yaw = g_render_world_yaw;
    return true;
}

// Once, on the first camera this composes. The angles the game asked for are
// printed whether Fov is set or not, because they are the number a player needs
// before they can choose one - and because a Fov that reads back as the angle
// the game was already drawing is how a setting the mod is not honouring gets
// caught. The view drawn first is whichever the player was in, so this is the
// cabin's angles or the chase camera's, not both.
void LogFieldOfViewOnce(const float* projection) {
    // Atomic because every detour composes on whichever render thread calls it.
    static std::atomic<bool> logged{false};
    if (logged.exchange(true, std::memory_order_relaxed)) return;

    if (!IsReportableProjection(projection)) {
        Log::Line("[camera] the projection at +0x%X does not read as a perspective one "
                  "(%.4f, %.4f), so Fov cannot be applied - please report this with your "
                  "game version",
                  g_projection_offset, projection[kProjectionHorizontal],
                  projection[kProjectionVertical]);
        return;
    }
    const float horizontal = ProjectionFovDegrees(projection[kProjectionHorizontal], 1.0f);
    const float vertical = ProjectionFovDegrees(projection[kProjectionVertical], 1.0f);
    if (g_fov_degrees <= 0.0f) {
        Log::Line("[camera] this view renders %.1f degrees across the frame and %.1f down it; "
                  "Fov is off, so the game's own Field of View settings are left alone",
                  horizontal, vertical);
        return;
    }
    const float scale = FovScaleForTarget(projection, g_fov_degrees);
    Log::Line("[camera] Fov=%.1f: horizontal %.1f -> %.1f degrees, vertical %.1f -> %.1f",
              g_fov_degrees, horizontal,
              ProjectionFovDegrees(projection[kProjectionHorizontal], scale),
              vertical,
              ProjectionFovDegrees(projection[kProjectionVertical], scale));
}

void ComposeRenderCamera(std::uint8_t* bytes, const HeadPose& pose, bool world_yaw) {
    auto* const view = reinterpret_cast<float*>(bytes + g_view_offset);
    auto* const projection = reinterpret_cast<float*>(bytes + g_projection_offset);
    auto* const eye = reinterpret_cast<float*>(bytes + g_eye_offset);
    auto* const view_projection = reinterpret_cast<float*>(bytes + g_view_projection_offset);
    LogFieldOfViewOnce(projection);
    ApplyHeadPoseToRenderCamera(view, eye, projection, view_projection,
                                *reinterpret_cast<float*>(bytes + g_vertical_fov_offset),
                                pose, world_yaw, g_fov_degrees);
    // The inverse routine writes all sixteen floats without reading the cache's
    // -1 dirty marker, so this one call both rebuilds and clears it.
    g_matrix_inverse(bytes + g_inverse_view_offset, 0, view);
}

void* __fastcall CameraFrustumDetour(void* camera, void* output, float far_plane) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (camera != g_player_camera()) {
        return g_original_camera_frustum(camera, output, far_plane);
    }
    HeadPose pose;
    bool world_yaw = false;
    if (!ShouldComposeCamera(caller, pose, world_yaw)) {
        return g_original_camera_frustum(camera, output, far_plane);
    }

    alignas(16) std::uint8_t tracked[kRenderCameraBytes];
    std::memcpy(tracked, camera, sizeof(tracked));
    ComposeRenderCamera(tracked, pose, world_yaw);
    ExpandCullingFrustum(reinterpret_cast<const float*>(tracked + g_view_offset),
                        reinterpret_cast<float*>(tracked + g_projection_offset),
                        reinterpret_cast<float*>(tracked + g_view_projection_offset),
                        *reinterpret_cast<float*>(tracked + g_vertical_fov_offset));
    return g_original_camera_frustum(tracked, output, far_plane);
}

void __fastcall RenderCameraUploadDetour(void* camera_data, void* context,
                                         void* bindings, int pass) {
    // Shadows use separate cameras; every player-view upload uses this record.
    const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (camera_data != g_player_camera()) {
        g_original_render_camera_upload(camera_data, context, bindings, pass);
        return;
    }

    HeadPose pose;
    bool world_yaw = false;
    if (!ShouldComposeCamera(caller, pose, world_yaw)) {
        g_original_render_camera_upload(camera_data, context, bindings, pass);
        return;
    }

    alignas(16) std::uint8_t tracked[kRenderCameraBytes];
    std::memcpy(tracked, camera_data, sizeof(tracked));
    ComposeRenderCamera(tracked, pose, world_yaw);
    g_original_render_camera_upload(tracked, context, bindings, pass);
}

// The main draw renders a list of extra views, and after each one re-uploads
// the player's camera through this second uploader. Left alone, every pass after
// it is drawn from the clean camera until the next upload, a frame that jumps
// back to where the head is not, on exactly the frames that render such a view.
void __fastcall RenderCameraUploadAltDetour(void* camera_data, void* holder, void* context) {
    const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (camera_data != g_player_camera()) {
        g_original_render_camera_upload_alt(camera_data, holder, context);
        return;
    }
    HeadPose pose;
    bool world_yaw = false;
    if (!ShouldComposeCamera(caller, pose, world_yaw)) {
        g_original_render_camera_upload_alt(camera_data, holder, context);
        return;
    }
    alignas(16) std::uint8_t tracked[kRenderCameraBytes];
    std::memcpy(tracked, camera_data, sizeof(tracked));
    ComposeRenderCamera(tracked, pose, world_yaw);
    g_original_render_camera_upload_alt(tracked, holder, context);
}

// World markers are placed by projecting through the record this stores, so
// the HUD gets the camera the frame is drawn with, head pose and Fov included,
// while the game's own snapshot is left as it built it.
void __fastcall UiCameraRefreshDetour(void* ui_camera) {
    g_original_ui_camera_refresh(ui_camera);
    auto* const stored = reinterpret_cast<std::uint8_t**>(
        static_cast<std::uint8_t*>(ui_camera) + g_ui_camera_record);
    const std::uint8_t* const snapshot = *stored;
    if (snapshot == nullptr) return;
    HeadPose pose;
    bool world_yaw = false;
    if (!ShouldComposeCamera(0, pose, world_yaw)) return;
    std::uint8_t* const tracked =
        g_ui_cameras[g_ui_camera_next.fetch_xor(1, std::memory_order_relaxed)];
    std::memcpy(tracked, snapshot, kRenderCameraBytes);
    ComposeRenderCamera(tracked, pose, world_yaw);
    *stored = tracked;
}

void __fastcall ViewRaysDetour(void* camera, void* output) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    // Motion blur pairs these rays with its clean historical view-projection.
    // Rotating only the current rays produces continuous blur at a held pose.
    if (caller == g_motion_blur_rays_return) {
        g_original_view_rays(camera, output);
        return;
    }
    HeadPose pose;
    bool world_yaw = false;
    if (camera != g_player_camera() || !ShouldComposeCamera(caller, pose, world_yaw)) {
        g_original_view_rays(camera, output);
        return;
    }
    alignas(16) std::uint8_t tracked[kRenderCameraBytes];
    std::memcpy(tracked, camera, sizeof(tracked));
    ComposeRenderCamera(tracked, pose, world_yaw);
    g_original_view_rays(tracked, output);
}

void __fastcall BoundsDetour(void* camera, void* minimum, void* maximum, float far_plane) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    HeadPose pose;
    bool world_yaw = false;
    if (camera != g_player_camera() || !ShouldComposeCamera(caller, pose, world_yaw)) {
        g_original_bounds(camera, minimum, maximum, far_plane);
        return;
    }
    alignas(16) std::uint8_t tracked[kRenderCameraBytes];
    std::memcpy(tracked, camera, sizeof(tracked));
    ComposeRenderCamera(tracked, pose, world_yaw);
    ExpandCullingFrustum(reinterpret_cast<const float*>(tracked + g_view_offset),
                        reinterpret_cast<float*>(tracked + g_projection_offset),
                        reinterpret_cast<float*>(tracked + g_view_projection_offset),
                        *reinterpret_cast<float*>(tracked + g_vertical_fov_offset));
    g_original_bounds(tracked, minimum, maximum, far_plane);
}

bool Failed(cameraunlock::hooks::HookStatus status, const char* what) {
    using cameraunlock::hooks::HookStatus;
    if (status == HookStatus::Ok) return false;
    Log::Line("[camera] %s failed: %s", what,
              cameraunlock::hooks::HookStatusToString(status));
    return true;
}

struct CameraHook {
    const char* what;
    std::uintptr_t rva;
    void* detour;
    void** original;
};

}  // namespace

bool InstallCameraHook(float fov_degrees) {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;

    g_fov_degrees = fov_degrees;
    const builds::BuildProfile& profile = builds::ActiveProfile();
    builds::MotionBlurDiscovery discovered;
    std::string discovery_error;
    if (!builds::DiscoverMotionBlurInModule(GetModuleHandleW(nullptr), discovered, discovery_error)) {
        Log::Line("[camera] motion-blur discovery rejected: %s", discovery_error.c_str());
        return false;
    }
    const struct { const char* name; unsigned int found; unsigned int expected; } checks[] = {
        {"player camera getter", discovered.player_camera_getter, profile.Offsets.player_camera_getter_rva},
        {"view rays", discovered.view_rays, profile.Offsets.camera_view_rays_rva},
        {"motion-blur caller", discovered.motion_blur_return, profile.Offsets.motion_blur_rays_return_rva},
        {"matrix inverse", discovered.inverse, profile.Offsets.matrix_inverse_rva},
        {"inverse-view field", discovered.inverse_view, profile.Offsets.render_inverse_view},
        {"vertical-FOV field", discovered.vertical_fov, profile.Offsets.render_vertical_fov},
    };
    for (const auto& check : checks) {
        if (check.found == check.expected) continue;
        Log::Line("[camera] discovered %s=%08X disagrees with %s=%08X; no camera hooks installed",
                  check.name, check.found, profile.Name, check.expected);
        return false;
    }
    Log::Line("[camera] discovered motion-blur return=%08X, rays=%08X, player getter=%08X, "
              "inverse=%08X, inverse field=%X, FOV=%X, aspect=%X; remaining dependencies use profile %s",
              discovered.motion_blur_return, discovered.view_rays, discovered.player_camera_getter,
              discovered.inverse, discovered.inverse_view, discovered.vertical_fov, discovered.aspect, profile.Name);
    g_module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    g_frustum_frame_return = g_module_base + profile.Offsets.frustum_frame_return_rva;
    g_player_camera = reinterpret_cast<PlayerCameraGetterFn>(
        g_module_base + discovered.player_camera_getter);
    g_matrix_inverse = reinterpret_cast<MatrixInverseFn>(
        g_module_base + profile.Offsets.matrix_inverse_rva);
    g_view_offset = profile.Offsets.render_view;
    g_projection_offset = profile.Offsets.render_projection;
    g_eye_offset = profile.Offsets.render_eye;
    g_view_projection_offset = profile.Offsets.render_view_projection;
    g_inverse_view_offset = profile.Offsets.render_inverse_view;
    g_vertical_fov_offset = profile.Offsets.render_vertical_fov;
    g_motion_blur_rays_return = g_module_base + discovered.motion_blur_return;
    g_ui_camera_record = profile.Offsets.ui_camera_record;

    std::string ui_camera_error;
    if (!CheckUiCameraRefresh(
            reinterpret_cast<const std::uint8_t*>(g_module_base + profile.Offsets.ui_camera_refresh_rva),
            reinterpret_cast<const std::uint8_t*>(g_module_base + discovered.player_camera_getter),
            g_ui_camera_record, ui_camera_error)) {
        Log::Line("[camera] %s (profile %s); no camera hooks installed", ui_camera_error.c_str(), profile.Name);
        return false;
    }
    std::string look_at_error;
    if (!CheckLookAt(reinterpret_cast<const std::uint8_t*>(g_module_base + profile.Offsets.render_camera_look_at_rva),
                     look_at_error)) {
        Log::Line("[camera] %s (profile %s); no camera hooks installed", look_at_error.c_str(), profile.Name);
        return false;
    }

    const CameraHook camera_hooks[] = {
        {"hooking the vehicle camera update", profile.Offsets.drive_camera_update_rva,
         reinterpret_cast<void*>(&DriveCameraUpdateDetour),
         reinterpret_cast<void**>(&g_original_drive_camera_update)},
        {"hooking the render camera upload", profile.Offsets.render_camera_upload_rva,
         reinterpret_cast<void*>(&RenderCameraUploadDetour),
         reinterpret_cast<void**>(&g_original_render_camera_upload)},
        {"hooking camera visibility", profile.Offsets.camera_frustum_rva,
         reinterpret_cast<void*>(&CameraFrustumDetour),
         reinterpret_cast<void**>(&g_original_camera_frustum)},
        {"hooking view rays", discovered.view_rays,
         reinterpret_cast<void*>(&ViewRaysDetour),
         reinterpret_cast<void**>(&g_original_view_rays)},
        {"hooking visibility bounds", profile.Offsets.camera_bounds_rva,
         reinterpret_cast<void*>(&BoundsDetour),
         reinterpret_cast<void**>(&g_original_bounds)},
        {"hooking the second render camera upload", profile.Offsets.render_camera_upload_alt_rva,
         reinterpret_cast<void*>(&RenderCameraUploadAltDetour),
         reinterpret_cast<void**>(&g_original_render_camera_upload_alt)},
        {"hooking the HUD camera", profile.Offsets.ui_camera_refresh_rva,
         reinterpret_cast<void*>(&UiCameraRefreshDetour),
         reinterpret_cast<void**>(&g_original_ui_camera_refresh)},
        {"hooking the render camera LookAt", profile.Offsets.render_camera_look_at_rva,
         reinterpret_cast<void*>(&LookAtDetour),
         reinterpret_cast<void**>(&g_original_look_at)},
    };
    constexpr std::size_t kHookCount = sizeof(camera_hooks) / sizeof(camera_hooks[0]);
    void* targets[kHookCount]{};

    HookManager& hooks = HookManager::Instance();
    const HookStatus initialized = hooks.Initialize();
    if (initialized != HookStatus::ErrorAlreadyInitialized
        && Failed(initialized, "MinHook init")) {
        return false;
    }

    // Every hook is created before any is enabled. Removing a hook that is
    // already live frees its trampoline under a render thread that may be inside
    // the detour on its way to calling it, so a failure here must only ever undo
    // hooks no thread has entered. Enabling them together also means no frame is
    // composed by some of these detours and culled by the originals.
    for (std::size_t i = 0; i < kHookCount; ++i) {
        void* const target = reinterpret_cast<void*>(g_module_base + camera_hooks[i].rva);
        if (Failed(hooks.CreateHook(target, camera_hooks[i].detour, camera_hooks[i].original),
                   camera_hooks[i].what)) {
            for (std::size_t created = 0; created < i; ++created) hooks.RemoveHook(targets[created]);
            return false;
        }
        targets[i] = target;
    }

    // One thread freeze for all of them; the co-op gate's hooks are already live and
    // MinHook skips them. Disabled rather than removed on failure: a partial
    // enable can leave a thread inside a detour, and disabling keeps its
    // trampoline valid.
    if (Failed(hooks.EnableAllHooks(), "enabling the camera hooks")) {
        for (void* const target : targets) hooks.DisableHook(target);
        return false;
    }

    Log::Line("[camera] hooked vehicle activity at 0x%p, player render uploads at 0x%p and 0x%p, "
              "visibility at 0x%p, view rays at 0x%p, bounds at 0x%p, HUD camera at 0x%p, LookAt at 0x%p (profile %s)",
              targets[0], targets[1], targets[5], targets[2], targets[3], targets[4], targets[6], targets[7],
              profile.Name);
    return true;
}

}  // namespace sr_ht
