// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <atomic>

extern "C" {
extern std::atomic<unsigned long long> sr_session_calls;
extern void* sr_session_original[12];
void sr_session_thunk_0();
void sr_session_thunk_1();
void sr_session_thunk_2();
void sr_session_thunk_3();
void sr_session_thunk_4();
void sr_session_thunk_5();
void sr_session_thunk_6();
void sr_session_thunk_7();
void sr_session_thunk_8();
void sr_session_thunk_9();
void sr_session_thunk_10();
void sr_session_thunk_11();
}

inline constexpr void (*kSessionThunks[])() = {
    sr_session_thunk_0, sr_session_thunk_1, sr_session_thunk_2, sr_session_thunk_3,
    sr_session_thunk_4, sr_session_thunk_5, sr_session_thunk_6, sr_session_thunk_7,
    sr_session_thunk_8, sr_session_thunk_9, sr_session_thunk_10, sr_session_thunk_11
};
