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

DriveCameraUpdateFn g_original_drive_camera_update = nullptr;
RenderCameraUploadFn g_original_render_camera_upload = nullptr;
CameraFrustumFn g_original_camera_frustum = nullptr;
PlayerCameraGetterFn g_player_camera = nullptr;
MatrixInverseFn g_matrix_inverse = nullptr;

std::uintptr_t g_module_base = 0;
std::uintptr_t g_primary_return = 0;
std::uintptr_t g_secondary_return = 0;
std::uintptr_t g_frustum_frame_return = 0;
unsigned g_view_offset = 0;
unsigned g_projection_offset = 0;
unsigned g_eye_offset = 0;
unsigned g_view_projection_offset = 0;
unsigned g_inverse_view_offset = 0;
constexpr unsigned kRenderCameraBytes = 0x134;

std::atomic<unsigned long long> g_last_drive_camera_tick{0};
std::atomic<bool> g_logged_drive_camera_activity{false};
constexpr unsigned long long kVehicleCameraActivityMs = 100;

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

bool PoseForPlayerCamera(std::uintptr_t caller, HeadPose& pose, bool& world_yaw) {
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
    if (!g_have_render_pose) return false;
    pose = g_render_pose;
    world_yaw = g_render_world_yaw;
    return true;
}

void ComposeRenderCamera(std::uint8_t* bytes, const HeadPose& pose, bool world_yaw) {
    auto* const view = reinterpret_cast<float*>(bytes + g_view_offset);
    auto* const projection = reinterpret_cast<float*>(bytes + g_projection_offset);
    auto* const eye = reinterpret_cast<float*>(bytes + g_eye_offset);
    auto* const view_projection = reinterpret_cast<float*>(bytes + g_view_projection_offset);
    auto* const inverse = bytes + g_inverse_view_offset;
    std::int32_t inverse_marker;
    std::memcpy(&inverse_marker, inverse, sizeof(inverse_marker));
    if (inverse_marker == -1) g_matrix_inverse(inverse, 0, view);
    ApplyHeadPoseToRenderCamera(view, eye, projection, view_projection, pose, world_yaw);
    g_matrix_inverse(inverse, 0, view);
}

void* __fastcall CameraFrustumDetour(void* camera, void* output, float far_plane) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (camera != g_player_camera()) {
        return g_original_camera_frustum(camera, output, far_plane);
    }
    HeadPose pose;
    bool world_yaw;
    if (!PoseForPlayerCamera(caller, pose, world_yaw)) {
        return g_original_camera_frustum(camera, output, far_plane);
    }

    alignas(16) std::uint8_t tracked[kRenderCameraBytes];
    std::memcpy(tracked, camera, sizeof(tracked));
    ComposeRenderCamera(tracked, pose, world_yaw);
    return g_original_camera_frustum(tracked, output, far_plane);
}

void __fastcall RenderCameraUploadDetour(void* camera_data, void* context,
                                         void* bindings, int pass) {
    const std::uintptr_t caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if (caller != g_primary_return && caller != g_secondary_return) {
        g_original_render_camera_upload(camera_data, context, bindings, pass);
        return;
    }

    HeadPose pose;
    bool world_yaw;
    if (!PoseForPlayerCamera(caller, pose, world_yaw)) {
        g_original_render_camera_upload(camera_data, context, bindings, pass);
        return;
    }

    alignas(16) std::uint8_t tracked[kRenderCameraBytes];
    std::memcpy(tracked, camera_data, sizeof(tracked));
    ComposeRenderCamera(tracked, pose, world_yaw);
    g_original_render_camera_upload(tracked, context, bindings, pass);
}

bool Failed(cameraunlock::hooks::HookStatus status, const char* what) {
    using cameraunlock::hooks::HookStatus;
    if (status == HookStatus::Ok) return false;
    Log::Line("[camera] %s failed: %s", what,
              cameraunlock::hooks::HookStatusToString(status));
    return true;
}

}  // namespace

bool InstallCameraHook() {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;

    const builds::BuildProfile& profile = builds::ActiveProfile();
    g_module_base = reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    g_primary_return = g_module_base + profile.Offsets.render_primary_return_rva;
    g_secondary_return = g_module_base + profile.Offsets.render_secondary_return_rva;
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

    void* const drive_target = reinterpret_cast<void*>(
        g_module_base + profile.Offsets.drive_camera_update_rva);
    void* const render_target = reinterpret_cast<void*>(
        g_module_base + profile.Offsets.render_camera_upload_rva);
    void* const frustum_target = reinterpret_cast<void*>(
        g_module_base + profile.Offsets.camera_frustum_rva);

    HookManager& hooks = HookManager::Instance();
    const HookStatus initialized = hooks.Initialize();
    if (initialized != HookStatus::ErrorAlreadyInitialized
        && Failed(initialized, "MinHook init")) {
        return false;
    }

    cameraunlock::hooks::ScopedHook drive_hook;
    cameraunlock::hooks::ScopedHook render_hook;
    cameraunlock::hooks::ScopedHook frustum_hook;
    if (Failed(drive_hook.Create(drive_target, reinterpret_cast<void*>(&DriveCameraUpdateDetour),
                                reinterpret_cast<void**>(&g_original_drive_camera_update)),
               "hooking the vehicle camera update")) {
        return false;
    }
    if (Failed(render_hook.Create(render_target, reinterpret_cast<void*>(&RenderCameraUploadDetour),
                                reinterpret_cast<void**>(&g_original_render_camera_upload)),
               "hooking the render camera upload")) {
        return false;
    }
    if (Failed(frustum_hook.Create(frustum_target, reinterpret_cast<void*>(&CameraFrustumDetour),
                                  reinterpret_cast<void**>(&g_original_camera_frustum)),
               "hooking camera visibility")) {
        return false;
    }
    drive_hook.Release();
    render_hook.Release();
    frustum_hook.Release();

    Log::Line("[camera] hooked vehicle activity at 0x%p, player render upload at 0x%p "
              "and visibility at 0x%p (profile %s)",
              drive_target, render_target, frustum_target, profile.Name);
    return true;
}

}  // namespace sr_ht
