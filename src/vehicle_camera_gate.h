// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>
#include <cstdint>

namespace sr_ht {

// Whether a frame is drawn from a vehicle camera, counted in frames rather than
// milliseconds. combineDriveCameraAction's update runs once per frame in
// gameplay and stops the frame a menu or loading screen opens. A wall-clock
// window cannot tell a menu from a slow frame: with a 100ms window every frame
// that took over 100ms was drawn without the head pose, a one-frame jump to the
// untracked view on every stutter (31 in 30 seconds of a hitching session).
//
// The update does not land at a fixed point relative to the frame's visibility
// pass (measured 0 to 47ms before it), so one frame can see no update and the
// next two. One quiet frame is therefore still a driving frame; two in a row is
// a menu.
class VehicleCameraGate {
public:
    // From the drive camera update, on whichever thread runs it.
    void OnDriveUpdate() { updates_.fetch_add(1, std::memory_order_release); }

    // Once per frame, before anything in that frame asks Active().
    void OnFrame() {
        const std::uint64_t updates = updates_.load(std::memory_order_acquire);
        if (updates != seen_) {
            seen_ = updates;
            quiet_frames_ = 0;
        } else if (quiet_frames_ < kQuietFramesAllowed + 1) {
            ++quiet_frames_;
        }
        active_ = seen_ != 0 && quiet_frames_ <= kQuietFramesAllowed;
    }

    bool Active() const { return active_; }

    static constexpr int kQuietFramesAllowed = 1;

private:
    std::atomic<std::uint64_t> updates_{0};
    std::uint64_t seen_ = 0;
    int quiet_frames_ = 0;
    bool active_ = false;
};

}  // namespace sr_ht
