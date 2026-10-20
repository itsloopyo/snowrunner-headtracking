// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "builds/build_registry.h"

#include "logging.h"

using cameraunlock::memory::PeFingerprint;

namespace sr_ht::builds {

const BuildProfile* const kKnownProfiles[] = {
    &kGdkProfile_20260924,
    &kSteamProfile_20260924,
    &kGdkProfile_20260722,
    &kSteamProfile_20260722,
};
const std::size_t kKnownProfileCount = sizeof(kKnownProfiles) / sizeof(kKnownProfiles[0]);

namespace {

const BuildProfile* g_active = nullptr;

// A TimeDateStamp is not a release date for this game, so the line does not
// guess whether the running build is older or newer than the known ones.
void LogUnknownBuild() {
    Log::Line("[build] no profile matches this SnowRunner.exe (newest known: %s). If the "
              "game updated recently, check the mod's releases page for an update.",
              kKnownProfiles[0]->Name);
    Log::Line("[build] staying fully dormant: no hooks installed, the game runs vanilla.");
}

}  // namespace

const BuildProfile& ActiveProfile() { return *g_active; }

ProfileSelection SelectProfile(void* moduleBase) {
    g_active = nullptr;
    PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(moduleBase, running)) {
        Log::Line("[build] could not read PE headers of the running module; staying dormant");
        return ProfileSelection::NoMatch;
    }

    Log::Line("[build] running EXE fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X",
              running.TimeDateStamp, running.SizeOfImage, running.CheckSum);

    for (std::size_t i = 0; i < kKnownProfileCount; ++i) {
        const BuildProfile& p = *kKnownProfiles[i];
        const bool match = running.Matches(p.Fingerprint);
        Log::Line("[build]   vs %-24s TimeDateStamp=0x%08X SizeOfImage=0x%08X CheckSum=0x%08X -> %s",
                  p.Name, p.Fingerprint.TimeDateStamp, p.Fingerprint.SizeOfImage,
                  p.Fingerprint.CheckSum, match ? "MATCH" : "no");
        if (!match) continue;

        if (!IsProfileComplete(p)) {
            Log::Line("[build] profile %s is a placeholder (offsets not derived yet); staying dormant", p.Name);
            return ProfileSelection::Incomplete;
        }
        g_active = &p;
        Log::Line("[build] activated profile %s", p.Name);
        return ProfileSelection::Matched;
    }

    LogUnknownBuild();
    return ProfileSelection::NoMatch;
}

}  // namespace sr_ht::builds
