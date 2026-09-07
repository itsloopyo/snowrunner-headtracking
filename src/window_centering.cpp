// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "window_centering.h"

#include <windows.h>

#include <cstdlib>

#include "logging.h"

#include "cameraunlock/os/game_window.h"

namespace sr_ht {

namespace {

namespace os = cameraunlock::os;

constexpr int kPollIntervalMs = 250;
constexpr int kPollAttempts = 240;  // 60s, which covers a cold start off a hard disk.

// The rect has to hold still before it is worth acting on. The window is up well
// before the engine has finished sizing and placing it, and when the engine
// stops moving it has not been measured here, so the wait is on three seconds of
// an unchanged rect rather than on a fixed delay that would be a guess.
constexpr int kSettlePolls = 12;

// The core's diagnostics carry their own "window:" topic, so they land under the
// same [boot] tag as everything else the bootstrap says.
void ForwardWindowLog(os::WindowLogLevel level, const char* message) {
    Log::Line("[boot] %s%s", level == os::WindowLogLevel::Warning ? "WARNING: " : "", message);
}

int CenteredOrigin(int area_start, int area_extent, int window_extent) {
    return area_start + (area_extent - window_extent) / 2;
}

bool IsCenteredOn(const RECT& window, const RECT& area) {
    // A game that centres its own window rounds the odd half-pixel up where the
    // integer maths here rounds it down, so an exact comparison would move the
    // window one pixel and report that as a fix.
    constexpr int kTolerance = 2;
    const int dx = window.left
                 - CenteredOrigin(area.left, area.right - area.left, window.right - window.left);
    const int dy = window.top
                 - CenteredOrigin(area.top, area.bottom - area.top, window.bottom - window.top);
    return std::abs(dx) <= kTolerance && std::abs(dy) <= kTolerance;
}

void CenterUnlessAlready(HWND window, const RECT& rect) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &info)) {
        Log::Line("[boot] WARNING: window: GetMonitorInfoW failed: %lu", GetLastError());
        return;
    }

    // Either reading counts as centred. A game centres on the monitor, this mod
    // centres on the work area, and the two differ by half the taskbar. Moving a
    // window that is already centred trades a visible jump for nothing, and a
    // fullscreen or borderless window is centred by definition, which is how it
    // is left alone here.
    if (IsCenteredOn(rect, info.rcWork) || IsCenteredOn(rect, info.rcMonitor)) {
        Log::Line("[boot] window: %dx%d at (%d, %d) is already centred, leaving it alone",
                  static_cast<int>(rect.right - rect.left),
                  static_cast<int>(rect.bottom - rect.top),
                  static_cast<int>(rect.left), static_cast<int>(rect.top));
        return;
    }

    os::CenterGameWindowOnce(&ForwardWindowLog);
}

// Waits for a game window whose rect has held still for kSettlePolls, writing it
// and that rect to the out-params. False when none settled in time, in which
// case the out-params are left alone.
bool WaitForSettledWindow(HWND& settled, RECT& settled_rect) {
    RECT previous{};
    bool have_previous = false;
    int stable_polls = 0;

    for (int attempt = 0; attempt < kPollAttempts; ++attempt) {
        Sleep(kPollIntervalMs);

        const HWND window = os::FindGameWindow();
        RECT current{};
        if (!window || !GetWindowRect(window, &current)) {
            have_previous = false;
            stable_polls = 0;
            continue;
        }

        if (have_previous && EqualRect(&previous, &current)) {
            if (++stable_polls < kSettlePolls) continue;
            settled = window;
            settled_rect = current;
            return true;
        }
        previous = current;
        have_previous = true;
        stable_polls = 0;
    }
    return false;
}

}  // namespace

void CenterWindowWhenReady() {
    HWND window = nullptr;
    RECT rect{};
    if (!WaitForSettledWindow(window, rect)) {
        Log::Line("[boot] window: no window settled within %ds, leaving placement alone",
                  kPollAttempts * kPollIntervalMs / 1000);
        return;
    }
    CenterUnlessAlready(window, rect);
}

}  // namespace sr_ht
