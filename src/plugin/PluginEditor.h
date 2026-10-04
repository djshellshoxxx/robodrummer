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
    juce::Slider bpm_;
    juce::Slider intensity_;
    juce::Label bpmCaption_;
    juce::Label intensityCaption_;
    juce::TextButton fillButton_{"FILL"};
    juce::TextButton resetButton_{"RESET PHASE"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoboDrummerAudioProcessorEditor)
};
