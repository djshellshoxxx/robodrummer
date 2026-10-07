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
    void setupPerformanceButton(juce::TextButton& button, robodrummer::MidiCommand command, const juce::String& tooltip);

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
    juce::Label meterCaption_;
    juce::Label styleCaption_;
    juce::Label silenceCaption_;
    juce::TextButton fillButton_{"FILL"};
    juce::TextButton crashButton_{"CRASH"};
    juce::TextButton breakButton_{"BREAK"};
    juce::TextButton halfTimeButton_{"HALF-TIME"};
    juce::TextButton doubleTimeButton_{"DOUBLE-TIME"};
    juce::TextButton stopButton_{"STOP"};
    juce::TextButton resumeButton_{"RESUME"};
    juce::TextButton previousSectionButton_{"PREV SECTION"};
    juce::TextButton nextSectionButton_{"NEXT SECTION"};
    juce::TextButton soloButton_{"SOLO"};
    juce::TextButton endJamButton_{"END JAM"};
    juce::TextButton resetButton_{"RESET LISTENING"};
    juce::TextButton helpButton_{"HELP"};
    juce::TextButton optionsButton_{"OPTIONS"};
    juce::TextButton closeHelpButton_{"CLOSE HELP"};
    juce::TextEditor helpText_;
    std::unique_ptr<juce::TooltipWindow> tooltipWindow_;
    bool tooltipsEnabled_{true};
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboBoxAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    // Declared after the controls so they are destroyed first.
    std::unique_ptr<SliderAttachment> bpmAttachment_, intensityAttachment_, leadershipAttachment_,
        followRangeAttachment_, dynamicFollowAttachment_, silenceStopBarsAttachment_;
    std::unique_ptr<ComboBoxAttachment> leadershipModeAttachment_, outputModeAttachment_, meterNumeratorAttachment_,
        meterDenominatorAttachment_, jamStyleAttachment_, silenceModeAttachment_;
    std::unique_ptr<ButtonAttachment> arrangementAttachment_, jamMemoryAttachment_, manualMeterAttachment_;
    std::uint32_t seenArrangementRevision_{0};
    int editingArrangementSlot_{0};
    bool loadingArrangementEditor_{false};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessorEditor)
};
