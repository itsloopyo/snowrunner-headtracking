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
#include "camera_fov.h"
#include "camera_transform.h"
#include "headtracking_mod.h"
#include "logging.h"

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

DriveCameraUpdateFn g_original_drive_camera_update = nullptr;
RenderCameraUploadFn g_original_render_camera_upload = nullptr;
CameraFrustumFn g_original_camera_frustum = nullptr;
PlayerCameraGetterFn g_player_camera = nullptr;
MatrixInverseFn g_matrix_inverse = nullptr;
ViewRaysFn g_original_view_rays = nullptr;
BoundsFn g_original_bounds = nullptr;

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

std::atomic<unsigned long long> g_last_drive_camera_tick{0};
std::atomic<bool> g_logged_drive_camera_activity{false};
constexpr unsigned long long kVehicleCameraActivityMs = 100;

// CameraUnlock.ini's Fov, the angle the frame should span across its width. Written once
// by InstallCameraHook before the first detour exists, so the render threads
// that read it can never see a half-built value.
float g_fov_degrees = 0.0f;

std::mutex g_pose_mutex;
HeadPose g_render_pose;
bool g_have_render_pose = false;
bool g_render_world_yaw = false;

void __fastcall DriveCameraUpdateDetour(void* camera, void* frame) {
    g_original_drive_camera_update(camera, frame);
    g_last_drive_camera_tick.store(GetTickCount64(), std::memory_order_release);
    if (!g_logged_drive_camera_activity.exchange(true, std::memory_order_relaxed)) {
        Log::Line("[camera] drive camera active");
    }
}

bool VehicleCameraIsActive() {
    const unsigned long long last = g_last_drive_camera_tick.load(std::memory_order_acquire);
    return last != 0 && GetTickCount64() - last <= kVehicleCameraActivityMs;
}

// This frame's head pose, and whether the camera has to be composed at all.
//
// A frame with no pose still composes while Fov is set: the field of view
// is a rendering setting rather than head tracking, so it survives the toggle
// hotkey, a tracker that has stopped sending and the co-op gate - all three of
// which only decide whether a pose exists. Both stop at the same place, a frame
// no vehicle camera has drawn for 100ms, which is a menu or a loading screen.
bool ShouldComposeCamera(std::uintptr_t caller, HeadPose& pose, bool& world_yaw) {
    std::lock_guard<std::mutex> lock(g_pose_mutex);
    if (!VehicleCameraIsActive()) {
        g_have_render_pose = false;
        return false;
    }
    if (caller == g_frustum_frame_return) {
        // Visibility must capture the pose before any draw uploads use it.
        g_have_render_pose = PoseForThisFrame(g_render_pose);
        g_render_world_yaw = WorldYawEnabled();
    }
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
    g_module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    g_frustum_frame_return = g_module_base + profile.Offsets.frustum_frame_return_rva;
    g_player_camera = reinterpret_cast<PlayerCameraGetterFn>(
        g_module_base + profile.Offsets.player_camera_getter_rva);
    g_matrix_inverse = reinterpret_cast<MatrixInverseFn>(
        g_module_base + profile.Offsets.matrix_inverse_rva);
    g_view_offset = profile.Offsets.render_view;
    g_projection_offset = profile.Offsets.render_projection;
    g_eye_offset = profile.Offsets.render_eye;
    g_view_projection_offset = profile.Offsets.render_view_projection;
    g_inverse_view_offset = profile.Offsets.render_inverse_view;
    g_vertical_fov_offset = profile.Offsets.render_vertical_fov;
    g_motion_blur_rays_return = g_module_base + profile.Offsets.motion_blur_rays_return_rva;

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
        {"hooking view rays", profile.Offsets.camera_view_rays_rva,
         reinterpret_cast<void*>(&ViewRaysDetour),
         reinterpret_cast<void**>(&g_original_view_rays)},
        {"hooking visibility bounds", profile.Offsets.camera_bounds_rva,
         reinterpret_cast<void*>(&BoundsDetour),
         reinterpret_cast<void**>(&g_original_bounds)},
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

    // One thread freeze for all five; the co-op gate's hooks are already live and
    // MinHook skips them. Disabled rather than removed on failure: a partial
    // enable can leave a thread inside a detour, and disabling keeps its
    // trampoline valid.
    if (Failed(hooks.EnableAllHooks(), "enabling the camera hooks")) {
        for (void* const target : targets) hooks.DisableHook(target);
        return false;
    }

    Log::Line("[camera] hooked vehicle activity at 0x%p, player render upload at 0x%p "
              "and visibility at 0x%p, view rays at 0x%p, bounds at 0x%p (profile %s)",
              targets[0], targets[1], targets[2], targets[3], targets[4], profile.Name);
    return true;
}

}  // namespace sr_ht
