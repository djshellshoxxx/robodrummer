// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary
#pragma once
#include <JuceHeader.h>
#include <vector>

class RoboDrummerAudioProcessor;

namespace robodrummer {

// Preset types from SPEC 31. Each type stores and recalls only its own slice of the plug-in state, so a Kit
// preset never changes the groove and a Drummer preset never changes the sounds.
enum class PresetType { Jam, Drummer, Style, Kit, Song, Analysis };
inline constexpr int kPresetTypeCount = 6;

struct PresetEntry {
    juce::String name;
    bool factory{false};
};

class PresetManager {
public:
    static const char* typeName(PresetType type) noexcept;
    static juce::File userDirectory(PresetType type);

    // Factory presets first, then user presets alphabetically.
    static std::vector<PresetEntry> list(PresetType type);
    static bool load(RoboDrummerAudioProcessor& processor, PresetType type, const PresetEntry& entry);
    static bool saveUser(RoboDrummerAudioProcessor& processor, PresetType type, const juce::String& name);
    static bool deleteUser(PresetType type, const juce::String& name);

    // Jam-type factory presets double as host "programs".
    static int factoryProgramCount();
    static juce::String factoryProgramName(int index);
    static bool loadFactoryProgram(RoboDrummerAudioProcessor& processor, int index);

    static juce::String sanitiseName(const juce::String& name);
};

} // namespace robodrummer
