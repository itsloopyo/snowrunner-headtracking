// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "window_centering.h"

#include <windows.h>
#include <cstdlib>

#include "logging.h"
#include "cameraunlock/os/game_window.h"

namespace sr_ht {
namespace {

constexpr int kPollIntervalMs = 250;
constexpr int kPollAttempts = 240;
constexpr int kSettlePolls = 12;

void CenterWindow(HWND window, const RECT& rect) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("[boot] WARNING: window: GetMonitorInfoW failed: %lu", GetLastError());
        return;
    }

    // A window spanning the entire monitor is fullscreen or borderless.
    if (rect.left <= info.rcMonitor.left && rect.top <= info.rcMonitor.top &&
        rect.right >= info.rcMonitor.right && rect.bottom >= info.rcMonitor.bottom) return;

    const LONG width = rect.right - rect.left;
    const LONG height = rect.bottom - rect.top;
    const LONG x = info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
    const LONG y = info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    if (std::abs(rect.left - x) <= 2 && std::abs(rect.top - y) <= 2) return;

    if (!SetWindowPos(window, nullptr, x, y, 0, 0,
                      SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE)) {
        Log::Line("[boot] WARNING: window: SetWindowPos failed: %lu", GetLastError());
        return;
    }
    Log::Line("[boot] window: centered %ldx%ld at (%ld, %ld)", width, height, x, y);
}

}  // namespace

void CenterWindowWhenReady() {
    HWND previous_window = nullptr;
    RECT previous{};
    HWND handled_window = nullptr;
    LONG handled_width = 0;
    LONG handled_height = 0;
    int stable_polls = 0;

    // Keep watching through startup: the initial window may settle before the
    // engine applies the player's resolution. Position-only changes after
    // centering are left alone so the player can still drag the window.
    for (int attempt = 0; attempt < kPollAttempts; ++attempt) {
        Sleep(kPollIntervalMs);
        const HWND window = cameraunlock::os::FindGameWindow();
        RECT current{};
        if (!window || IsIconic(window) || IsZoomed(window)) {
            previous_window = nullptr;
            stable_polls = 0;
            continue;
        }
        if (!GetWindowRect(window, &current)) {
            Log::Line("[boot] WARNING: window: GetWindowRect failed: %lu", GetLastError());
            return;
        }
        if (window != previous_window || !EqualRect(&previous, &current)) {
            previous_window = window;
            previous = current;
            stable_polls = 0;
            continue;
        }
        if (stable_polls < kSettlePolls) ++stable_polls;
        if (stable_polls != kSettlePolls) continue;

        const LONG width = current.right - current.left;
        const LONG height = current.bottom - current.top;
        if (window == handled_window && width == handled_width && height == handled_height) continue;
        CenterWindow(window, current);
        handled_window = window;
        handled_width = width;
        handled_height = height;
    }
}

}  // namespace sr_ht
