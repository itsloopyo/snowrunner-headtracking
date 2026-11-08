// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>
#include <cstdint>

namespace sr_ht {

// Whether a frame is drawn from a vehicle camera: whether combineDriveCameraAction's
// update has run lately. It runs on a 60 Hz tick while driving (measured 60 a
// second at 140 to 220 fps, and more than one a frame on average below 60 fps: 43
// at 32 fps), and stops when a menu or loading screen opens.
//
// "Lately" has to hold at both ends. Counted only in frames, one quiet frame
// allowed, it shut on the third frame of every tick that had one, so a quarter of
// the frames at 160 fps were drawn from the untracked camera. Counted only in
// milliseconds, a 100ms window shut on every frame that took longer than that, a
// one-frame jump to the untracked view on every stutter (31 in 30 seconds of a
// hitching session). So the gate stays open while an update was seen within the
// last kQuietFramesAllowed + 1 frames OR within the last kQuietMs.
//
// The update also does not land at a fixed point relative to the frame's
// visibility pass (measured 0 to 47ms before it), so one frame can see no update
// and the next two, which the frame allowance covers.
class VehicleCameraGate {
public:
    // From the drive camera update, on whichever thread runs it.
    void OnDriveUpdate() { updates_.fetch_add(1, std::memory_order_release); }

    // Once per frame, before anything in that frame asks Active(). now_ms is any
    // monotonic millisecond clock.
    void OnFrame(std::uint64_t now_ms) {
        const std::uint64_t updates = updates_.load(std::memory_order_acquire);
        if (updates != seen_) {
            seen_ = updates;
            quiet_frames_ = 0;
            last_update_ms_ = now_ms;
        } else if (quiet_frames_ < kQuietFramesAllowed + 1) {
            ++quiet_frames_;
        }
        active_ = seen_ != 0 &&
                  (quiet_frames_ <= kQuietFramesAllowed || now_ms - last_update_ms_ <= kQuietMs);
    }

    bool Active() const { return active_; }

    static constexpr int kQuietFramesAllowed = 1;
    static constexpr std::uint64_t kQuietMs = 100;

private:
    std::atomic<std::uint64_t> updates_{0};
    std::uint64_t seen_ = 0;
    int quiet_frames_ = 0;
    std::uint64_t last_update_ms_ = 0;
    bool active_ = false;
};

}  // namespace sr_ht
