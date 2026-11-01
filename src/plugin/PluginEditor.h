// RoboDrummer™
// Copyright © 2026 Sheldon Davidson. All rights reserved.
// Proprietary source code. See LICENSE and COPYRIGHT-TRADEMARK.md.
// SPDX-License-Identifier: LicenseRef-Proprietary


#pragma once
#include <JuceHeader.h>
#include "EditorPanels.h"
#include "PluginProcessor.h"
#include "PresetManager.h"
#include "ReducedMotionPreference.h"

class RoboDrummerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer {
public:
    explicit RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor&);
    RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor&, std::function<std::optional<bool>()> platformMotionReader);
    ~RoboDrummerAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

    static constexpr int kMinWidth = 960, kMinHeight = 680, kDefaultWidth = 1100, kDefaultHeight = 740;
    static constexpr int TabCount = 9;
    static const char* tabName(int index);

    // Test and tooling hooks.
    void showTabForTest(int index) { selectTab(index); }
    int currentTab() const noexcept { return currentTab_; }
    bool isHelpOpen() const noexcept { return helpOverlay_ != nullptr && helpOverlay_->isVisible(); }
    void openHelpForTest() { showHelp(); }
    void closeHelpForTest() { hideHelp(); }
    bool isTourRunning() const noexcept { return tour_ != nullptr && tour_->isRunning(); }
    void startTourForTest() { startTour(); }
    rdui::FocusRing& focusRing() noexcept { return focusRing_; }
    void setReducedMotionForTest(bool on) { applyReducedMotion(on); }
    void setHighContrastForTest(bool on) { applyHighContrast(on); }
    int timerRateHz() const noexcept { return timerHz_; }

private:
    void timerCallback() override;
    void selectTab(int index);
    void showHelp();
    void hideHelp();
    void startTour();
    void applyTooltipSetting(bool enabled);
    void applyTooltipDelay(int ms);
    void applyReducedMotion(bool on);
    void applyHighContrast(bool on);
    void setTimerRate(int hz);
    void rebuildPresetList();
    void promptSavePreset();

    RoboDrummerAudioProcessor& processor_;
    rdui::RoboLookAndFeel lookAndFeel_;
    juce::Label title_;
    juce::Label subtitle_;
    juce::TextButton helpButton_{"HELP"};
    juce::ComboBox presetType_, presetList_;
    juce::TextButton presetSave_{"SAVE"}, presetDelete_{"DELETE"};
    std::vector<robodrummer::PresetEntry> presetEntries_;

    std::array<juce::TextButton, TabCount> tabButtons_;
    std::array<std::unique_ptr<rdui::PanelBase>, TabCount> panels_;
    int currentTab_{0};

    std::unique_ptr<rdui::HelpOverlay> helpOverlay_;
    std::unique_ptr<rdui::TourCard> tour_;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;
    int tooltipDelayMs_{600};
    int timerHz_{20};
    juce::Label footer_;
    rdui::FocusRing focusRing_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessorEditor)
};
