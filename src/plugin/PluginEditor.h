#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class RoboDrummerAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                              private juce::Timer {
public:
    explicit RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor&);
    ~RoboDrummerAudioProcessorEditor() override = default;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void loadArrangementEditorSlot();
    void commitArrangementEditorSlot();
    void showHelp();
    void showOptions();
    void applyTooltipSetting();

    RoboDrummerAudioProcessor& processor_;
    juce::Label title_;
    juce::Label tempoLabel_;
    juce::Label transportLabel_;
    juce::Label guitarLabel_;
    juce::Label trackingLabel_;
    juce::Label authorityLabel_;
    juce::Label dynamicsLabel_;
    juce::Label phraseLabel_;
    juce::Label sectionLabel_;
    juce::Label memoryLabel_;
    juce::Label silenceLabel_;
    juce::Label coordinatorLabel_;
    juce::Slider bpm_;
    juce::Slider intensity_;
    juce::Slider leadership_;
    juce::Slider followRange_;
    juce::Slider dynamicFollow_;
    juce::Slider silenceStopBars_;
    juce::ComboBox leadershipMode_;
    juce::ComboBox outputMode_;
    juce::ComboBox fillLength_;
    juce::ComboBox meterNumerator_;
    juce::ComboBox meterDenominator_;
    juce::ComboBox jamStyle_;
    juce::ComboBox silenceMode_;
    juce::ToggleButton arrangementToggle_{"Programmed arrangement"};
    juce::ToggleButton jamMemoryToggle_{"Learn this jam"};
    juce::ToggleButton manualMeterToggle_{"Manual meter"};
    juce::ComboBox arrangementSlot_;
    juce::ComboBox arrangementStyle_;
    juce::Slider arrangementBars_;
    juce::Slider arrangementIntensity_;
    juce::ToggleButton arrangementSlotEnabled_{"Enabled"};
    juce::ToggleButton arrangementAutoAdvance_{"Auto advance"};
    juce::Label arrangementEditCaption_;
    juce::Label bpmCaption_;
    juce::Label intensityCaption_;
    juce::Label leadershipCaption_;
    juce::Label followRangeCaption_;
    juce::Label dynamicFollowCaption_;
    juce::Label modeCaption_;
    juce::Label outputModeCaption_;
    juce::Label fillLengthCaption_;
    juce::Label meterCaption_;
    juce::Label styleCaption_;
    juce::Label silenceCaption_;
    juce::TextButton fillButton_{"FILL"};
    juce::TextButton resetButton_{"RESET LISTENING"};
    juce::TextButton helpButton_{"HELP"};
    juce::TextButton optionsButton_{"OPTIONS"};
    juce::TextButton closeHelpButton_{"CLOSE HELP"};
    juce::TextEditor helpText_;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;
    bool tooltipsEnabled_{true};
    int editingArrangementSlot_{0};
    bool loadingArrangementEditor_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessorEditor)
};
