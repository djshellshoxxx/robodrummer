#include "PluginEditor.h"

RoboDrummerAudioProcessorEditor::RoboDrummerAudioProcessorEditor(RoboDrummerAudioProcessor& p)
    : AudioProcessorEditor(&p), processor_(p) {
    setSize(660, 430);

    title_.setText("RoboDrummer", juce::dontSendNotification);
    title_.setFont(juce::Font(28.0f, juce::Font::bold));
    title_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(title_);

    bpmCaption_.setText("Internal BPM", juce::dontSendNotification);
    intensityCaption_.setText("Intensity", juce::dontSendNotification);
    addAndMakeVisible(bpmCaption_);
    addAndMakeVisible(intensityCaption_);

    bpm_.setRange(40.0, 240.0, 0.1);
    bpm_.setValue(processor_.getInternalBpm(), juce::dontSendNotification);
    bpm_.setSliderStyle(juce::Slider::LinearHorizontal);
    bpm_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
    bpm_.onValueChange = [this] { processor_.setInternalBpm(bpm_.getValue()); };
    addAndMakeVisible(bpm_);

    intensity_.setRange(0.0, 1.0, 0.01);
    intensity_.setValue(processor_.getIntensity(), juce::dontSendNotification);
    intensity_.setSliderStyle(juce::Slider::LinearHorizontal);
    intensity_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 24);
    intensity_.onValueChange = [this] { processor_.setIntensity(static_cast<float>(intensity_.getValue())); };
    addAndMakeVisible(intensity_);

    tempoLabel_.setText("Drummer tempo: --", juce::dontSendNotification);
    transportLabel_.setText("Transport: internal", juce::dontSendNotification);
    guitarLabel_.setText("Guitar estimate: listening", juce::dontSendNotification);
    trackingLabel_.setText("Tracker: acquiring", juce::dontSendNotification);
    addAndMakeVisible(tempoLabel_);
    addAndMakeVisible(transportLabel_);
    addAndMakeVisible(guitarLabel_);
    addAndMakeVisible(trackingLabel_);

    fillButton_.onClick = [this] { processor_.requestFill(); };
    resetButton_.onClick = [this] { processor_.resetJamPhase(); };
    addAndMakeVisible(fillButton_);
    addAndMakeVisible(resetButton_);

    startTimerHz(12);
}

void RoboDrummerAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(juce::Colour::fromRGB(17, 20, 25));
    g.setColour(juce::Colour::fromRGB(55, 68, 80));
    g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(10.0f), 12.0f, 1.0f);
    g.setColour(juce::Colour::fromRGB(190, 198, 205));
    g.setFont(13.0f);
    g.drawText("Phase 3 telemetry: causal guitar onset/tempo/phase tracking is active. Drummer timing remains host/internal until tracker validation is complete.",
               24, 385, getWidth() - 48, 24, juce::Justification::centredLeft);
}

void RoboDrummerAudioProcessorEditor::resized() {
    auto area = getLocalBounds().reduced(24);
    title_.setBounds(area.removeFromTop(42));
    area.removeFromTop(10);

    auto row = area.removeFromTop(42);
    bpmCaption_.setBounds(row.removeFromLeft(110));
    bpm_.setBounds(row);

    area.removeFromTop(8);
    row = area.removeFromTop(42);
    intensityCaption_.setBounds(row.removeFromLeft(110));
    intensity_.setBounds(row);

    area.removeFromTop(18);
    tempoLabel_.setBounds(area.removeFromTop(28));
    transportLabel_.setBounds(area.removeFromTop(28));
    guitarLabel_.setBounds(area.removeFromTop(28));
    trackingLabel_.setBounds(area.removeFromTop(28));

    area.removeFromTop(18);
    auto buttons = area.removeFromTop(44);
    fillButton_.setBounds(buttons.removeFromLeft(160));
    buttons.removeFromLeft(12);
    resetButton_.setBounds(buttons.removeFromLeft(190));
}

void RoboDrummerAudioProcessorEditor::timerCallback() {
    const auto t = processor_.getLastTransport();
    const double shownBpm = t.validTempo ? t.bpm : processor_.getInternalBpm();
    tempoLabel_.setText("Drummer tempo: " + juce::String(shownBpm, 1) + " BPM", juce::dontSendNotification);
    transportLabel_.setText(
        t.validTempo ? (juce::String("Transport: host | ") + (t.playing ? "playing" : "stopped"))
                     : "Transport: internal fallback",
        juce::dontSendNotification);

    const auto detected = processor_.getDetectedGuitarBpm();
    const auto tempoConfidence = processor_.getGuitarTempoConfidence();
    const auto beatConfidence = processor_.getGuitarBeatConfidence();
    guitarLabel_.setText(
        "Guitar estimate: " + juce::String(detected, 1) + " BPM | tempo confidence " +
            juce::String(tempoConfidence * 100.0f, 0) + "%",
        juce::dontSendNotification);
    trackingLabel_.setText(
        juce::String("Tracker: ") + (processor_.isGuitarTrackerLocked() ? "LOCKED" : "acquiring") +
            " | beat confidence " + juce::String(beatConfidence * 100.0f, 0) + "% | phase " +
            juce::String(processor_.getGuitarBeatPhase(), 2),
        juce::dontSendNotification);
}
