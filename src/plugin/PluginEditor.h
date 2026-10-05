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

    RoboDrummerAudioProcessor& processor_;
    juce::Label title_;
    juce::Label tempoLabel_;
    juce::Label transportLabel_;
    juce::Label guitarLabel_;
    juce::Label trackingLabel_;
    juce::Label authorityLabel_;
    juce::Label dynamicsLabel_;
    juce::Label phraseLabel_;
    juce::Slider bpm_;
    juce::Slider intensity_;
    juce::Slider leadership_;
    juce::Slider followRange_;
    juce::Slider dynamicFollow_;
    juce::ComboBox leadershipMode_;
    juce::ComboBox jamStyle_;
    juce::Label bpmCaption_;
    juce::Label intensityCaption_;
    juce::Label leadershipCaption_;
    juce::Label followRangeCaption_;
    juce::Label dynamicFollowCaption_;
    juce::Label modeCaption_;
    juce::Label styleCaption_;
    juce::TextButton fillButton_{"FILL"};
    juce::TextButton resetButton_{"RESET LISTENING"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessorEditor)
};
