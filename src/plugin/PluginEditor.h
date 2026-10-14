// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary


#pragma once
#include <JuceHeader.h>
#include "EditorPanels.h"
#include "PluginProcessor.h"
#include "PresetManager.h"

class RoboDrummerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer {
public:
    explicit RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor&);
    ~RoboDrummerAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void selectTab(int index);
    void showHelp();
    void applyTooltipSetting(bool enabled);
    void rebuildPresetList();
    void promptSavePreset();
    static juce::File settingsFile();

    RoboDrummerAudioProcessor& processor_;
    rdui::RoboLookAndFeel lookAndFeel_;
    juce::Label title_;
    juce::Label subtitle_;
    juce::TextButton helpButton_{"HELP"};
    juce::ComboBox presetType_, presetList_;
    juce::TextButton presetSave_{"SAVE"}, presetDelete_{"DELETE"};
    std::vector<robodrummer::PresetEntry> presetEntries_;

    static constexpr int TabCount = 8;
    std::array<juce::TextButton, TabCount> tabButtons_;
    std::array<std::unique_ptr<rdui::PanelBase>, TabCount> panels_;
    int currentTab_{0};

    juce::TextEditor helpText_;
    juce::TextButton closeHelpButton_{"CLOSE HELP"};
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;
    juce::Label footer_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessorEditor)
};
