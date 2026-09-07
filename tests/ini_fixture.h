// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

// The temp directory every config test drives LoadConfig against. The loader
// takes a directory and finds HeadTracking.ini inside it, so a test cannot pass
// it a file - it needs a directory of its own, per process, that nothing else
// is writing into.

#include "test_support.h"

#include <windows.h>

#include <cstdio>
#include <string>

namespace sr_test {

inline std::string IniPathIn(const std::string& dir) {
    return dir + "\\HeadTracking.ini";
}

inline std::string LogPathIn(const std::string& dir) {
    return dir + "\\HeadTracking.log";
}

// A directory of this process's own under %TEMP%, named for the suite so two
// suites running at once cannot share one. Empty on failure, with the reason
// already counted as a test failure.
inline std::string MakeTempDir(const char* suite) {
    char temp[MAX_PATH]{};
    const DWORD n = GetTempPathA(MAX_PATH, temp);
    if (n == 0 || n >= MAX_PATH) {
        std::printf("  FAIL: GetTempPathA failed (%lu)\n", GetLastError());
        ++g_failures;
        return {};
    }
    std::string dir = std::string(temp, n) + "sr-ht-" + suite + "-test-"
                    + std::to_string(GetCurrentProcessId());
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

// Removes the directory and the three files the mod is able to put in it.
// Deleting one that was never written is a no-op, so this suits a suite that
// exercises the log and one that does not alike. A suite that opened the log
// closes it first - a file still held open refuses to delete, and the directory
// then survives with it.
inline void RemoveTempDir(const std::string& dir) {
    if (dir.empty()) return;
    DeleteFileA(IniPathIn(dir).c_str());
    DeleteFileA(LogPathIn(dir).c_str());
    DeleteFileA((dir + "\\HeadTracking.prev.log").c_str());
    RemoveDirectoryA(dir.c_str());
}

// Lays down `body` as the whole HeadTracking.ini, replacing whatever the
// previous case wrote. False - with the reason counted as a failure - when the
// file could not be written, because a case that silently reuses the previous
// case's INI passes for the wrong reason.
inline bool WriteIni(const std::string& dir, const char* body) {
    FILE* f = nullptr;
    fopen_s(&f, IniPathIn(dir).c_str(), "w");
    if (f == nullptr) {
        std::printf("  FAIL: could not write %s\n", IniPathIn(dir).c_str());
        ++g_failures;
        return false;
    }
    std::fputs(body, f);
    std::fclose(f);
    return true;
}

}  // namespace sr_test
