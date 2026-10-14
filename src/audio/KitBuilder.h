// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <JuceHeader.h>
#include "audio/DrumSamplePlayer.h"
#include <memory>

namespace robodrummer {

inline constexpr int kBuiltInKitCount = 4;
inline constexpr int kUserKitIndex = kBuiltInKitCount;
inline constexpr int kKitChoiceCount = kBuiltInKitCount + 1;
inline constexpr std::array<const char*, kKitChoiceCount> kKitNames {
    "Studio (built-in)", "Tight (built-in)", "Big Room (built-in)", "Lo-Fi (built-in)", "User Folder"
};

// Builds sampler instances off the audio thread. Users can drop WAV/AIFF files into a folder
// (kick.wav, snare_v1.wav ... snare_v3.wav, snare_v2_rr2.wav, closedhat.wav, ...); anything missing is
// filled in from the built-in kit so a partial kit is still a playable one.
class KitBuilder {
public:
    static std::unique_ptr<DrumSamplePlayer> buildBuiltIn(int kitIndex, double sampleRate);
    static std::unique_ptr<DrumSamplePlayer> loadUserFolder(const juce::File& folder, double sampleRate, juce::String& report);
};

} // namespace robodrummer
